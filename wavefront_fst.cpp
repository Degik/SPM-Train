#include <sys/time.h>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <cstdlib>
#include <numeric>
#include <vector>
#include <thread>
#include <memory>
#include <mutex>
#include <list>

#include <ff/ff.hpp>
#include <ff/farm.hpp>
#include <ff/parallel_for.hpp>
#include <ff/pipeline.hpp>

using namespace std;
using namespace ff;


using vector_d = vector<double>;
using matrix_d = vector<vector<double>>;
using tuple_dot_product = tuple<vector_d, vector_d, matrix_d, uint16_t, uint16_t, uint16_t, int, int>;

mutex mtx; // Mutex for the print

typedef struct Resources{
    uint16_t Z; // Z = Workers for the M-Diagonal-Stage
    uint16_t D; // D = Workers for the Dot-Product-Stage
} Resources;


// CalculateResources
/*!
    \name CalculateResources
    \brief Calculate the resources for the workers
    \note Calculate the resources for the workers based on the values of W, K, N and return the resources for the workers (Z, D)
*/
Resources CalculateResources(uint16_t W, uint16_t K, uint16_t N){
    Resources resources = {0, 0};
    if (K < W){
        resources.D = W / 2;
        resources.Z = W / 2;
    } else {
        resources.D = (float)(W / 2) + ((float)(W / 2) * ((float)(K - 1) / (float)K));
        resources.Z = W - resources.D;
    }
    return resources;
}



// FillMatrix
/*!
    \name FillMatrix
    \brief Fill the matrix M with the values
    \note Fill the matrix M with the values
*/
matrix_d& FillMatrix(matrix_d& M, uint16_t N, uint16_t W){
    ParallelFor pf(W); // Create the parallel for object with W workers
    // Fill the diagonal elements (i,j) (where i == j) with (m+1)/N
    pf.parallel_for(0, N, [&](const long m){
        M[m][m] = static_cast<double>(m+1)/N;
    });
    return M;
}

// Print matrix M
/*!
    \name PrintMatrix
    \brief Print the matrix M
    \note Print the matrix M
*/
void PrintMatrix(matrix_d& M, uint16_t N){
    for(int i = 0; i < N; i++){
        for(int j = 0; j < N; j++){
            printf("%.6f ", M[i][j]);
        }
        printf("\n");
    }
}

/*!
    \name SaveMatrixToFile
    \brief Save the matrix M to a file
    \note Save the matrix M to a file with the name filename
*/
void SaveMatrixToFile(matrix_d& M, uint16_t N, string filename){
    ofstream file;
    file.open(filename);
    for(int i = 0; i < N; i++){
        for(int j = 0; j < N; j++){
            printf("%.6f ", M[i][j]);
        }
        file << endl;
    }
    file.close();
}

/*!
    \name PartialDotProduct
    \brief Calculate the partial dot product of the vectors
    \note Calculate the partial dot product of the vectors v1 and v2 with the size
*/
double PartialDotProduct(const vector_d &v1, const vector_d &v2, uint16_t size){
    double partial_sum = 0.0;
    
    for(uint16_t idx = 0; idx < size; idx++){
        partial_sum += v1[idx] * v2[idx];
    }
    return partial_sum;
}


/*!
    \name SplitVector
    \note This function takes one vector and returns a list with the sub-vectors
       \n The last element can have a different size

*/
list<vector_d> SplitVector(vector_d v, uint16_t D, uint16_t K){
    size_t base_size = D / K;
    size_t remainder = D % K;
    size_t start = 0;

    // All sub vectors are stored in this list
    list<vector_d> splitted_vectors;

    for (size_t i = 0; i < K; ++i) {
        size_t current_size = base_size + (i < remainder ? 1 : 0);
        splitted_vectors.push_back(vector_d(v.begin() + start, v.begin() + start + current_size));
        start += current_size;
    }

    return splitted_vectors;
}

// FastFlow Functions
//
/*!
    \name CreateMatrix
    \brief Create the matrix M
    \note Create the matrix M with the size N and fill it with the values
*/
struct CreateMatrix: ff_node_t<int, matrix_d> {
    uint16_t N, W;
    CreateMatrix(uint16_t size, uint16_t workers): N(size), W(workers) {} // Constructor
    
    matrix_d *svc(int *task){ // Service
        matrix_d M (N, vector_d(N, 0.0)); // Create the matrix
        M = FillMatrix(M, N, W);     // Fill the matrix
        PrintMatrix(M, N);           // Print the matrix
        SaveMatrixToFile(M, N, "matrix.txt");      // Save the matrix to a file
        ff_send_out(new matrix_d(M));             // Send the matrix to the next stage
        return EOS;
    }
    
};

/*!
    \name Sink
    \brief Calculate the cbrt(sum) and update the matrix
*/
struct Sink: ff_minode_t<double, double>{
    matrix_d& M; // Reference to the matrix
    int i, j;    // Indices of the matrix to update
    double sum = 0.0;
    Sink(matrix_d& M, int i, int j) : M(M), i(i), j(j) {}

    double* svc(double* task){
        sum += *task;
        delete task;
        return EOS;
    }

    void svc_end(){ // matrix_d, int(i), int(j)
        // Update the matrix
        const double element = cbrt(sum);
        M[i][j] = element;
        M[j][i] = element;
        mtx.lock();
        cout << "Ho aggiornato la matrice con il valore: " << element << " nella posizione M[" << i << "][" << j << "]" << endl;
        mtx.unlock();
    }
};

/*!
    \name DotProduct_Emitter
    \brief Split the vectors v1 and v2 for the workers
    \note Split the vectors v1 and v2 for the workers
*/
struct DotProduct_Emitter: ff_node_t<tuple<vector_d, vector_d, int>>{
    uint16_t K, W, D;
    const vector_d& v1, v2;
    
    DotProduct_Emitter(uint16_t K, uint16_t W, uint16_t D, const vector_d& v1, const vector_d& v2)
        : K(K), W(W), D(D), v1(v1), v2(v2) {}
    
    tuple<vector_d, vector_d, int>* svc(tuple<vector_d, vector_d, int>*){
        //
        list<vector_d> sub_v1_list(D);
        list<vector_d> sub_v2_list(D);
        //
        // if K is less or equal with D we can have 1:(1,1) 
        // [one workers:for one elememt] for calculate the dot product
        sub_v1_list = SplitVector(v1, D, K);
        sub_v2_list = SplitVector(v2, D, K);
        //
        auto begin_v1 = sub_v1_list.begin();
        auto begin_v2 = sub_v2_list.begin();
        for(uint16_t w = 0; w < D; w++, begin_v1++, begin_v2++){
            // Take the sub-vectors and send them to the workers
            const uint16_t size = begin_v1->size(); // v1 and v2 have the same size
            ff_send_out(new tuple<vector_d, vector_d, int>(*begin_v1, *begin_v2, size));
            cout << "Tuple sent (DotProduct)" << endl;
        }

        return EOS;
    }
};

/*!
    \name Diagonal_Emitter
    \brief Take the matrix M and split it for the workers
    \note Take the matrix M and split it for the workers
*/
struct Diagonal_Emitter: ff_node_t<tuple_dot_product>{
    matrix_d& M;
    uint16_t N, K, W, D;

    Diagonal_Emitter(matrix_d& M, uint16_t N, uint16_t K, uint16_t W, uint16_t D)
        : M(M), N(N), K(K), W(W), D(D) {}

    tuple_dot_product* svc(tuple_dot_product*){
        cout << "m = [0, " << N - K << "[" << endl;
        for (int m = 0; m < N - K; m++){
            cout << "Taking v1 and v2 vectors for m: " << m << endl;
            vector_d v1;
            vector_d v2;
            for (int i = 0; i < K; i++){
                v1.push_back(M[m][m+i]);
                cout << "M[" << m << "][" << m+i << "]: " << M[m][i] << endl;
                v2.push_back(M[m+K][m+i]);
                cout << "M[" << m+K << "][" << m+i << "]: " << M[i][m+K] << endl;
            }
            cout << "v1: { ";
            for(auto i : v1){
                cout << i << " ";
            }
            cout << "}" << endl;

            cout << "v2: { ";
            for(auto i : v2){
                cout << i << " ";
            }
            cout << "}" << endl;
            cout << "Sending the tuple to the farm" << endl;
            ff_send_out(new tuple_dot_product(v1, v2, M, K, W, D, m, m+K));
            cout << "Tuple sent" << endl;
        }
        return EOS;
    }
};

// DotProduct_Worker
// Take the tuple (v1, v2, start, ending)
/*!
    \brief Calculate the dot product fof the sub vector
    \return double value (dot product)
*/
struct DotProduct_Worker: ff_node_t<tuple<vector_d, vector_d, int>, double>{
    double* svc(tuple<vector_d, vector_d, int> *task){
        const vector_d& v1 = std::get<0>(*task);
        const vector_d& v2 = std::get<1>(*task);
        const int size = std::get<2>(*task);

        double partial_result = PartialDotProduct(v1, v2, size);
        cout << "Partial result: " << partial_result << endl;
        ff_send_out(new double(partial_result)); // send the result
        return GO_ON;
    }

};

/*!
    \name DotProductStage
    \brief Take the v1 and v2 and split them for calculate the dot product
    \note From the v1 and v2 vectors split each one vector into v1-sub-vectors and v2-sub-vectors for N-vectors
       \n It depends from the D (workers)  (N) number of split = K / D or N = K (Optimal case)
       \n Call DotProduct_Worker for calculate each sub dot product on this sub vectors
       \n Take all the results and calculate cbrt(sum) and then return the value
*/
struct DotProduct_Stage: ff_node_t<tuple_dot_product, double> {
    // Take the vectors (v1, v2) created on the previous stage
    double* svc(tuple_dot_product *task){
        const vector_d& v1 = std::get<0>(*task);
        const vector_d& v2 = std::get<1>(*task);
        matrix_d& M = std::get<2>(*task);
        const uint16_t K = std::get<3>(*task); // K
        const uint16_t W = std::get<4>(*task); // Number of workers
        const uint16_t D = std::get<5>(*task); // Number of workers for the DotProduct
        const int i = std::get<6>(*task); // i
        const int j = std::get<7>(*task); // j


        vector<unique_ptr<ff_node>> workers;

        for(uint16_t w = 0; w < D; w++){
            workers.push_back(make_unique<DotProduct_Worker>());
        }

        Sink sink(M, i, j);                             // Create the sink
        DotProduct_Emitter emitter(K, W, D, v1, v2); // Create the emitter
        ff_Farm<tuple<vector_d, vector_d, int>> farm(move(workers));
        // Add the emitter and the sink
        farm.add_emitter(emitter);
        farm.add_collector(sink);
        cout << "Farm created (DotProduct)" << endl;

        //farm.wrap_around();
        farm.set_scheduling_ondemand();
        if (farm.run_and_wait_end() < 0){
            error("Running farm (DotProduct)\n");
            return EOS;
        }
        return GO_ON;
    }
};

struct M_Diagonal_Stage: ff_node_t<matrix_d, matrix_d> { // Take matrix M and return M'
    uint16_t N, K, W;
    uint16_t Z, D;

    M_Diagonal_Stage(uint16_t N, uint16_t K, uint16_t W, uint16_t Z, uint16_t D): N(N), K(K), W(W), Z(Z), D(D) {}

    matrix_d *svc(matrix_d *M) {
        
        vector<unique_ptr<ff_node>> workers;
        // Cycle with m with m = [0, n-k[
        for (uint16_t w = 0; w < Z; w++){
            cout << "Worker - M: " << w << endl;
            workers.push_back(make_unique<DotProduct_Stage>());
        }
        Diagonal_Emitter emitter(*M, N, K, W, D);
        ff_Farm<tuple_dot_product, matrix_d> farm(move(workers), emitter);
        cout << "Farm created (M-Diagonal)" << endl;
        farm.wrap_around();
        //farm.remove_collector();
        farm.set_scheduling_ondemand();
        
        if (farm.run_and_wait_end() < 0){
            error("Running farm (M-Diagonal)\n");
            return EOS;
        }

        // cout << "Farm runned (M-Diagonal)" << endl;

        // for (uint16_t m = 0 ; m < N - K; m++){
        //     cout << "Taking v1 and v2 vectors for m: " << m << endl;
        //     vector_d v1;
        //     vector_d v2;
        //     // Take the vectors v1 and v2 starting from position m and m+K to K
        //     // Fill the v1 vector
        //     for (uint16_t i = 0; i < K; i++){
        //         v1.push_back((*M)[m][i]);
        //         cout << "M[" << m << "][" << i << "]: " << (*M)[m][i] << endl;
        //     }
        //     for (uint16_t i = 0; i < K; i++){
        //         v2.push_back((*M)[m+K][i]);
        //         cout << "M[" << i << "][" << m+K << "]: " << (*M)[i][m+K] << endl;
        //     }
        //     cout << "Sending the tuple to the farm" << endl;
        //     auto task = new tuple_dot_product(v1, v2, *M, K, W, D, m, m+K);
        //     farm.ff_send_out(task);
        //     cout << "Tuple sent" << endl;
        // }
        // farm.wait();
        // delete M;

        if (N-1 == K){
            return M;
        }
        return GO_ON;
    }
};

struct SaveMatrix_Stage: ff_node_t<matrix_d>{
    matrix_d* svc(matrix_d* task){
        uint16_t size = task->size();
        SaveMatrixToFile(*task, size, "matrix_prime.txt");
        return EOS;
    }
};

int main(int argc, char* arg[]){
    // N, W
    if (argc != 3) {
        cout << "Usage: " << arg[0] << "N (Size N*N) W (Workers)" << endl;
        return -1;
    }

    const uint16_t N = atoi(arg[1]);
    const uint16_t W = atoi(arg[2]);

    cout << "N: " << N << " W: " << W << endl;


    ffTime(START_TIME);

    // Stage 1
    // Create the matrix M
    // Fill the matrix M with the values (m+1)/N
    CreateMatrix s1(N, W);
    //
    ff_Pipe<> pipe;
    //pipe.add_stage(s1);
    // Stage 2
    // Calculate the dot product of the matrix
    for (uint16_t k = 1; k < N; k++){
        // Calculate how to split the workers W
        Resources resources = CalculateResources(W, k, N);

        cout << "K = " << k << endl;
        cout << "DiagonalStage-Workers Z: " << resources.Z << " DotProductStage-Workers D: " << resources.D << endl;
        
        //M_Diagonal_Stage* s2 = new M_Diagonal_Stage(N, k, W, resources.Z, resources.D);
        M_Diagonal_Stage s2(N, k, W, resources.Z, resources.D);

        pipe.add_stage(s2);
        
    }
    // Save the matrix to a file
    //SaveMatrix_Stage s3;
    //pipe.add_stage(s3);
    //int task = 0;
    //s1.svc(&task);
    if (pipe.run_and_wait_end() < 0){
        error("Running pipe");
        return -1;
    }
    ffTime(STOP_TIME);
    cout << "Time: " << ffTime(GET_TIME)/1000.0 << endl;

    // CREATE MATRIX M (Stage 1)
    // STAGE 1 create all subelements of the matrix for the diagoanl
    // STAGE 2 create the dot product of the matrix

    // SPLIT THE PROBLEME WITH THE PIPE()
    // CREATE THE FARM FOR GENERETING THE DOT PRODUCT
}

