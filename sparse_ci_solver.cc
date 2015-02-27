#include <cmath>

#include <boost/timer.hpp>
#include <boost/format.hpp>

#include <libciomr/libciomr.h>
#include <libqt/qt.h>

#include "sparse_ci_solver.h"


namespace psi{ namespace libadaptive{


void SigmaVectorFull::compute_sigma(Matrix& sigma, Matrix &b, int nroot){
    sigma.gemm(false,true,1.0,H_,b,0.0);
}

void SigmaVectorFull::get_diagonal(Vector& diag){
    double** h = H_->pointer();
    for (int I = 0; I < size_; ++I){
        diag.set(I,h[I][I]);
    }
}

void SigmaVectorSparse::compute_sigma(Matrix& sigma, Matrix &b, int nroot){
    double** sigma_p = sigma.pointer();
    double** b_p = b.pointer();
    for (int J = 0; J < size_; ++J){
        for (int r = 0; r < nroot; ++r){
            sigma_p[J][r] = 0.0;
        }
        std::vector<double>& H_row = H_[J].second;
        std::vector<int>& index_row = H_[J].first;
        size_t maxc = index_row.size();
        for (int c = 0; c < maxc; ++c){
            int K = index_row[c];
            double HJK = H_row[c];
            for (int r = 0; r < nroot; ++r){
                sigma_p[J][r] +=  HJK * b_p[r][K];
            }
        }
    }
}

void SigmaVectorSparse::get_diagonal(Vector& diag){
    for (int I = 0; I < size_; ++I){
        diag.set(I,H_[I].second[0]);
    }
}

SigmaVectorList::SigmaVectorList(const std::vector<BitsetDeterminant>& space, ExplorerIntegrals *ints)
    : SigmaVector(space.size()), ints_(ints)
{

    typedef std::map<BitsetDeterminant,size_t>::iterator bstmap_it;

    size_t max_I = space.size();

    size_t na_ann = 0;
    size_t nb_ann = 0;
    std::map<BitsetDeterminant,size_t> map_a_ann;
    std::map<BitsetDeterminant,size_t> map_b_ann;

    a_ann_list.resize(max_I);
    b_ann_list.resize(max_I);

    outfile->Printf("\n  Generating determinants with N-1 electrons.\n");
    for (size_t I = 0; I < max_I; ++I){
        BitsetDeterminant detI = space[I];

        std::vector<int> aocc = detI.get_alfa_occ();
        std::vector<int> bocc = detI.get_beta_occ();

        int noalpha = aocc.size();
        int nobeta  = bocc.size();

        std::vector<std::pair<size_t,int>> a_ann(noalpha);
        std::vector<std::pair<size_t,int>> b_ann(nobeta);

        // Generate alpha annihilation
        for (int i = 0; i < noalpha; ++i){
            int ii = aocc[i];
            BitsetDeterminant detJ(detI);
            detJ.set_alfa_bit(ii,false);

            const boost::dynamic_bitset<>& Ia = detI.alfa_bits();
            double sign = SlaterSign(Ia,ii);

            bstmap_it it = map_a_ann.find(detJ);
            size_t detJ_add;
            // detJ is not in the map, add it
            if (it == map_a_ann.end()){
                detJ_add = na_ann;
                map_a_ann[detJ] = na_ann;
                na_ann++;
            }else{
                detJ_add = it->second;
            }
            a_ann[i] = std::make_pair(detJ_add,(sign > 0.5) ? (ii + 1) : (-ii-1));
        }
        a_ann_list[I] = a_ann;
        // Generate beta annihilation
        for (int i = 0; i < nobeta; ++i){
            int ii = bocc[i];
            BitsetDeterminant detJ(detI);
            detJ.set_beta_bit(ii,false);

            const boost::dynamic_bitset<>& Ib = detI.beta_bits();
            double sign = SlaterSign(Ib,ii);

            bstmap_it it = map_b_ann.find(detJ);
            size_t detJ_add;
            // detJ is not in the map, add it
            if (it == map_b_ann.end()){
                detJ_add = nb_ann;
                map_b_ann[detJ] = nb_ann;
                nb_ann++;
            }else{
                detJ_add = it->second;
            }
            b_ann[i] = std::make_pair(detJ_add,(sign > 0.5) ? (ii + 1) : (-ii-1));
        }
        b_ann_list[I] = b_ann;
    }

    a_cre_list.resize(map_a_ann.size());
    b_cre_list.resize(map_b_ann.size());


    for (size_t I = 0; I < max_I; ++I){
        const std::vector<std::pair<size_t,int>>& a_ann = a_ann_list[I];
        for (const std::pair<size_t,int>& J_sign : a_ann){
            size_t J = J_sign.first;
            int sign = J_sign.second;
            a_cre_list[J].push_back(std::make_pair(I,sign));
        }
        const std::vector<std::pair<size_t,int>>& b_ann = b_ann_list[I];
        for (const std::pair<size_t,int>& J_sign : b_ann){
            size_t J = J_sign.first;
            int sign = J_sign.second;
            b_cre_list[J].push_back(std::make_pair(I,sign));
        }
    }
    outfile->Printf("\n  Size of lists:");
    outfile->Printf("\n  |I> ->  a_p |I>: %zu",a_ann_list.size());
    outfile->Printf("\n  |I> ->  a_p |I>: %zu",b_ann_list.size());
    outfile->Printf("\n  |A> -> a+_q |A>: %zu",a_cre_list.size());
    outfile->Printf("\n  |A> -> a+_q |A>: %zu",b_cre_list.size());
}

void SigmaVectorList::compute_sigma(Matrix& sigma, Matrix& b, int nroot)
{
}

void SigmaVectorList::get_diagonal(Vector& diag)
{
}

void SparseCISolver::diagonalize_hamiltonian(const std::vector<BitsetDeterminant>& space,SharedVector& evals,SharedMatrix& evecs,int nroot,DiagonalizationMethod diag_method)
{
    if (space.size() < 50){
        diagonalize_full(space,evals,evecs,nroot);
    }else{
        if (diag_method == Full){
            diagonalize_full(space,evals,evecs,nroot);
        }else if (diag_method == DavidsonLiuDense){
            diagonalize_davidson_liu_dense(space,evals,evecs,nroot);
        }else if (diag_method == DavidsonLiuSparse){
            diagonalize_davidson_liu_sparse(space,evals,evecs,nroot);
        }
    }
}

void SparseCISolver::diagonalize_hamiltonian(const std::vector<SharedBitsetDeterminant>& space,SharedVector& evals,SharedMatrix& evecs,int nroot,DiagonalizationMethod diag_method)
{
    if (space.size() < 50){
        diagonalize_full(space,evals,evecs,nroot);
    }else{
        if (diag_method == Full){
            diagonalize_full(space,evals,evecs,nroot);
        }else if (diag_method == DavidsonLiuDense){
            diagonalize_davidson_liu_dense(space,evals,evecs,nroot);
        }else if (diag_method == DavidsonLiuSparse){
            diagonalize_davidson_liu_sparse(space,evals,evecs,nroot);
        }
    }
}

void SparseCISolver::diagonalize_full(const std::vector<BitsetDeterminant>& space,SharedVector& evals,SharedMatrix& evecs,int nroot)
{
    // Find all the eigenvalues and eigenvectors of the Hamiltonian
    SharedMatrix H = build_full_hamiltonian(space);

    size_t dim_space = space.size();
    evecs.reset(new Matrix("U",dim_space,dim_space));
    evals.reset(new Vector("e",dim_space));

    // Diagonalize H
    boost::timer t_diag;
    H->diagonalize(evecs,evals);
    outfile->Printf("\n  %s: %f s","Time spent diagonalizing H",t_diag.elapsed());
}


void SparseCISolver::diagonalize_davidson_liu_dense(const std::vector<BitsetDeterminant>& space,SharedVector& evals,SharedMatrix& evecs,int nroot)
{
    outfile->Printf("\n  Using <diagonalize_davidson_liu_dense>");
    outfile->Flush();
    // Find all the eigenvalues and eigenvectors of the Hamiltonian
    SharedMatrix H = build_full_hamiltonian(space);

    size_t dim_space = space.size();
    evecs.reset(new Matrix("U",dim_space,nroot));
    evals.reset(new Vector("e",nroot));

    // Diagonalize H
    SigmaVectorFull svf (H);
    SigmaVector* sigma_vector = &svf;
    davidson_liu(sigma_vector,evals,evecs,nroot);
}

void SparseCISolver::diagonalize_davidson_liu_sparse(const std::vector<BitsetDeterminant>& space,SharedVector& evals,SharedMatrix& evecs,int nroot)
{
    outfile->Printf("\n\n  Davidson-liu sparse algorithm");
    outfile->Flush();
    // Find all the eigenvalues and eigenvectors of the Hamiltonian
    std::vector<std::pair<std::vector<int>,std::vector<double>>> H = parallel_ ? build_sparse_hamiltonian_parallel(space) : build_sparse_hamiltonian(space);

    size_t dim_space = space.size();
    evecs.reset(new Matrix("U",dim_space,nroot));
    evals.reset(new Vector("e",nroot));

    // Diagonalize H
    SigmaVectorSparse svs (H);
    SigmaVector* sigma_vector = &svs;
    davidson_liu(sigma_vector,evals,evecs,nroot);
}

SharedMatrix SparseCISolver::build_full_hamiltonian(const std::vector<BitsetDeterminant> &space)
{
    // Build the H matrix
    size_t dim_space = space.size();
    SharedMatrix H(new Matrix("H",dim_space,dim_space));

#pragma omp parallel for schedule(dynamic)
    for (size_t I = 0; I < dim_space; ++I){
        const BitsetDeterminant& detI = space[I];
        for (size_t J = I; J < dim_space; ++J){
            const BitsetDeterminant& detJ = space[J];
            double HIJ = detI.slater_rules(detJ);
            H->set(I,J,HIJ);
            H->set(J,I,HIJ);
        }
    }
    return H;
}

std::vector<std::pair<std::vector<int>,std::vector<double>>> SparseCISolver::build_sparse_hamiltonian(const std::vector<BitsetDeterminant> &space)
{
    boost::timer t_h_build2;
    std::vector<std::pair<std::vector<int>,std::vector<double>>> H_sparse;
    size_t dim_space = space.size();

    size_t num_nonzero = 0;
    // Form the Hamiltonian matrix
    for (size_t I = 0; I < dim_space; ++I){
        std::vector<double> H_row;
        std::vector<int> index_row;
        const BitsetDeterminant& detI = space[I];
        double HII = detI.slater_rules(detI);
        H_row.push_back(HII);
        index_row.push_back(I);
        for (size_t J = 0; J < dim_space; ++J){
            if (I != J){
                const BitsetDeterminant& detJ = space[J];
                double HIJ = detI.slater_rules(detJ);
                if (std::fabs(HIJ) >= 1.0e-12){
                    H_row.push_back(HIJ);
                    index_row.push_back(J);
                    num_nonzero += 1;
                }
            }
        }
        H_sparse.push_back(make_pair(index_row,H_row));
    }
    outfile->Printf("\n  The sparse Hamiltonian matrix contains %zu nonzero elements out of %zu (%f)",num_nonzero,dim_space * dim_space,double(num_nonzero)/double(dim_space * dim_space));
    outfile->Printf("\n  %s: %f s","Time spent building H",t_h_build2.elapsed());
    outfile->Flush();
    return H_sparse;
}


std::vector<std::pair<std::vector<int>,std::vector<double>>> SparseCISolver::build_sparse_hamiltonian_parallel(const std::vector<BitsetDeterminant> &space)
{
    boost::timer t_h_build2;
    // Allocate as many elements as we need
    size_t dim_space = space.size();
    std::vector<std::pair<std::vector<int>,std::vector<double>>> H_sparse(dim_space);

    size_t num_nonzero = 0;

    outfile->Printf("\n  Building H using OpenMP-take2");
    outfile->Flush();

    // Form the Hamiltonian matrix
#pragma omp parallel for schedule(dynamic)
    for (size_t I = 0; I < dim_space; ++I){
        std::vector<double> H_row;
        std::vector<int> index_row;
        const BitsetDeterminant& detI = space[I];
        double HII = detI.slater_rules(detI);
        H_row.push_back(HII);
        index_row.push_back(I);
        for (size_t J = 0; J < dim_space; ++J){
            if (I != J){
                const BitsetDeterminant detJ = space[J];
                double HIJ = detI.slater_rules(detJ);
                if (std::fabs(HIJ) >= 1.0e-12){
                    H_row.push_back(HIJ);
                    index_row.push_back(J);
                }
            }
        }
#pragma omp critical(save_h_row)
        {
            H_sparse[I] = make_pair(index_row,H_row);
            num_nonzero += index_row.size();
        }
    }
    outfile->Printf("\n  The sparse Hamiltonian matrix contains %zu nonzero elements out of %zu (%f)",num_nonzero,dim_space * dim_space,double(num_nonzero)/double(dim_space * dim_space));
    outfile->Printf("\n  %s: %f s","Time spent building H (openmp)",t_h_build2.elapsed());
    outfile->Flush();
    return H_sparse;
}

void SparseCISolver::diagonalize_full(const std::vector<SharedBitsetDeterminant>& space,SharedVector& evals,SharedMatrix& evecs,int nroot)
{
    // Find all the eigenvalues and eigenvectors of the Hamiltonian
    SharedMatrix H = build_full_hamiltonian(space);

    size_t dim_space = space.size();
    evecs.reset(new Matrix("U",dim_space,dim_space));
    evals.reset(new Vector("e",dim_space));

    // Diagonalize H
    boost::timer t_diag;
    H->diagonalize(evecs,evals);
    outfile->Printf("\n  %s: %f s","Time spent diagonalizing H",t_diag.elapsed());
}


void SparseCISolver::diagonalize_davidson_liu_dense(const std::vector<SharedBitsetDeterminant>& space,SharedVector& evals,SharedMatrix& evecs,int nroot)
{
    outfile->Printf("\n  Using <diagonalize_davidson_liu_dense>");
    outfile->Flush();
    // Find all the eigenvalues and eigenvectors of the Hamiltonian
    SharedMatrix H = build_full_hamiltonian(space);

    size_t dim_space = space.size();
    evecs.reset(new Matrix("U",dim_space,nroot));
    evals.reset(new Vector("e",nroot));

    // Diagonalize H
    SigmaVectorFull svf (H);
    SigmaVector* sigma_vector = &svf;
    davidson_liu(sigma_vector,evals,evecs,nroot);
}

void SparseCISolver::diagonalize_davidson_liu_sparse(const std::vector<SharedBitsetDeterminant>& space,SharedVector& evals,SharedMatrix& evecs,int nroot)
{
    outfile->Printf("\n\n  Davidson-liu sparse algorithm");
    outfile->Flush();
    // Find all the eigenvalues and eigenvectors of the Hamiltonian
    std::vector<std::pair<std::vector<int>,std::vector<double>>> H = build_sparse_hamiltonian(space);

    size_t dim_space = space.size();
    evecs.reset(new Matrix("U",dim_space,nroot));
    evals.reset(new Vector("e",nroot));

    // Diagonalize H
    SigmaVectorSparse svs (H);
    SigmaVector* sigma_vector = &svs;
    davidson_liu(sigma_vector,evals,evecs,nroot);
}

SharedMatrix SparseCISolver::build_full_hamiltonian(const std::vector<SharedBitsetDeterminant> &space)
{
    // Build the H matrix
    size_t dim_space = space.size();
    SharedMatrix H(new Matrix("H",dim_space,dim_space));

#pragma omp parallel for schedule(dynamic)
    for (size_t I = 0; I < dim_space; ++I){
        SharedBitsetDeterminant detI = space[I];
        for (size_t J = I; J < dim_space; ++J){
            SharedBitsetDeterminant detJ = space[J];
            double HIJ = detI->slater_rules(*detJ);
            H->set(I,J,HIJ);
            H->set(J,I,HIJ);
        }
    }
    return H;
}

std::vector<std::pair<std::vector<int>,std::vector<double>>> SparseCISolver::build_sparse_hamiltonian(const std::vector<SharedBitsetDeterminant> &space)
{
    boost::timer t_h_build2;
    std::vector<std::pair<std::vector<int>,std::vector<double>>> H_sparse;
    size_t dim_space = space.size();

    size_t num_nonzero = 0;
    // Form the Hamiltonian matrix
    for (size_t I = 0; I < dim_space; ++I){
        std::vector<double> H_row;
        std::vector<int> index_row;
        SharedBitsetDeterminant detI = space[I];
        double HII = detI->slater_rules(*detI);
        H_row.push_back(HII);
        index_row.push_back(I);
        for (size_t J = 0; J < dim_space; ++J){
            if (I != J){
                SharedBitsetDeterminant detJ = space[J];
                double HIJ = detI->slater_rules(*detJ);
                if (std::fabs(HIJ) >= 1.0e-12){
                    H_row.push_back(HIJ);
                    index_row.push_back(J);
                    num_nonzero += 1;
                }
            }
        }
        H_sparse.push_back(make_pair(index_row,H_row));
    }
    outfile->Printf("\n  The sparse Hamiltonian matrix contains %zu nonzero elements out of %zu (%f)",num_nonzero,dim_space * dim_space,double(num_nonzero)/double(dim_space * dim_space));
    outfile->Printf("\n  %s: %f s","Time spent building H",t_h_build2.elapsed());
    outfile->Flush();
    return H_sparse;
}


std::vector<std::pair<std::vector<int>,SharedVector>> SparseCISolver::build_sparse_hamiltonian2(const std::vector<SharedBitsetDeterminant> &space)
{
    boost::timer t_h_build2;
    std::vector<std::pair<std::vector<int>,SharedVector>> H_sparse;
    size_t dim_space = space.size();

    size_t num_nonzero = 0;
    // Form the Hamiltonian matrix
    for (size_t I = 0; I < dim_space; ++I){
        std::vector<double> H_row;
        std::vector<int> index_row;
        SharedBitsetDeterminant detI = space[I];
        double HII = detI->slater_rules(*detI);
        H_row.push_back(HII);
        index_row.push_back(I);
        for (size_t J = 0; J < dim_space; ++J){
            if (I != J){
                SharedBitsetDeterminant detJ = space[J];
                double HIJ = detI->slater_rules(*detJ);
                if (std::fabs(HIJ) >= 1.0e-12){
                    H_row.push_back(HIJ);
                    index_row.push_back(J);
                    num_nonzero += 1;
                }
            }
        }
//        H_sparse.push_back(make_pair(index_row,H_row));
    }
    outfile->Printf("\n  The sparse Hamiltonian matrix contains %zu nonzero elements out of %zu (%f)",num_nonzero,dim_space * dim_space,double(num_nonzero)/double(dim_space * dim_space));
    outfile->Printf("\n  %s: %f s","Time spent building H",t_h_build2.elapsed());
    outfile->Flush();
    return H_sparse;
}


bool SparseCISolver::davidson_liu(SigmaVector* sigma_vector, SharedVector Eigenvalues, SharedMatrix Eigenvectors, int nroot_s)
{
    // Start a timer
    boost::timer t_davidson;

    int maxiter = 100;
    bool print = false;

    // Use unit vectors as initial guesses
    int N = sigma_vector->size();
    int M = nroot_s;
    int maxdim = 12 * M;

    double* eps = Eigenvalues->pointer();
    double** v = Eigenvectors->pointer();

    double cutoff = 1.0e-10;

    if (print_details_){
        outfile->Printf("\n  Size of the Hamiltonian: %d x %d",N,N);
    }

    // current set of guess vectors stored by row
    Matrix b("b",maxdim,N);
    b.zero();
    // guess vectors formed from old vectors, stored by row
    Matrix bnew("bnew",M,N);

    // residual eigenvectors, stored by row
    Matrix f("f",maxdim,N);

    // sigma vectors, stored by column
    Matrix sigma("sigma",N, maxdim);

    // Davidson mini-Hamitonian
    Matrix G("G",maxdim, maxdim);
    // A metric matrix
    Matrix S("S",maxdim, maxdim);
    // Eigenvectors of the Davidson mini-Hamitonian
    Matrix alpha("alpha",maxdim, maxdim);
    // Eigenvalues of the Davidson mini-Hamitonian
    Vector lambda("lambda",maxdim);
    // Old eigenvalues of the Davidson mini-Hamitonian
    Vector lambda_old("lambda",maxdim);

    Vector Adiag("Adiag",N);
    sigma_vector->get_diagonal(Adiag);

    // Find the M lowest diagonals
    double minimum = Adiag.get(0);
    int min_pos = 0;
    for(int i = 0; i < M; i++) {
        for(int j = 1; j < N; j++){
            if(Adiag.get(j) < minimum) {
                minimum = Adiag.get(j);
                min_pos = j;
            }
        }
        b.set(i,min_pos,1.0);
        Adiag.set(min_pos,BIGNUM);
    }

    int init_dim = M;
    int L = init_dim;
    int iter = 0;
    int converged = 0;
    while((converged < M) and (iter < maxiter)) {
        bool skip_check = false;
        if(print) outfile->Printf("\n  iter = %d\n", iter);

        // Step #2: Build and Diagonalize the Subspace Hamiltonian
        sigma_vector->compute_sigma(sigma,b,maxdim);


        G.gemm(false,false,1.0,b,sigma,0.0);

//        G.print();

//        for (int r = 0; r < L; ++r){
//            for (int I = 0; I < N; ++I){
//                if (std::fabs(b.get(r,I)) > 0.05){
//                    outfile->Printf("\n  sol = %d, N = %d, c = %f",r,I,b.get(r,I));
//                }
//            }
//        }
//        if (L == M) G.print();

        // diagonalize mini-matrix
        G.diagonalize(alpha,lambda);

        // Davidson mini-Hamitonian
        S.gemm(false,true,1.0,b,b,0.0);

//        S.print();

        bool printed_S = false;
        // Check for orthogonality
        for (int i = 0; i < L; ++i){
            double diag = S.get(i,i);
            double zero = false;
            double one = false;
            if (std::fabs(diag - 1.0) < 1.e-6){
                one = true;
            }
            if (std::fabs(diag) < 1.e-6){
                zero = true;
            }
            if ((not zero) and (not one)){
                if (not printed_S) {
                    S.print();
                    printed_S = true;
                }
                outfile->Printf("\n  WARNING: Vector %d is not normalized or zero");
            }
            double offdiag = 0.0;
            for (int j = i + 1; j < L; ++j){
                offdiag += std::fabs(S.get(i,j));
            }
            if (offdiag > 1.0e-6){
                if (not printed_S) {
                    S.print();
                    printed_S = true;
                }
                outfile->Printf("\n  WARNING: The vectors are not orthogonal");

            }
        }

        // Step #3: Build the Correction Vectors
        // form preconditioned residue vectors
        f.zero();
        double* lambda_p = lambda.pointer();
        double* Adiag_p = Adiag.pointer();
        double** b_p = b.pointer();
        double** f_p = f.pointer();
        double** alpha_p = alpha.pointer();
        double** sigma_p = sigma.pointer();
        for(int k = 0; k < M; k++){  // loop over roots
            for(int I = 0; I < N; I++) {  // loop over elements
                for(int i = 0; i < L; i++) {
                    f_p[k][I] += alpha_p[i][k] * (sigma_p[I][i] - lambda_p[k] * b_p[i][I]);
                }
                double denom = lambda_p[k] - Adiag_p[I];
                if(fabs(denom) > 1e-6) f_p[k][I] /= denom;
                else f_p[k][I] = 0.0;
            }
        }

        // Step #4: Orthonormalize the Correction Vectors
        /* normalize each residual */
        for(int k = 0; k < M; k++) {
            double norm = 0.0;
            for(int I = 0; I < N; I++) {
                norm += f_p[k][I] * f_p[k][I];
            }
            norm = std::sqrt(norm);
            if(norm < 1.0e-6){
                outfile->Printf("\n  WARNING norm of Davidson residual is less than 1.0e-6");
                for(int I = 0; I < N; I++) {
                    f_p[k][I] = 0.0;
                }
            }else{
                for(int I = 0; I < N; I++) {
                    f_p[k][I] /= norm;
                }
            }
        }

        // schmidt orthogonalize the f[k] against the set of b[i] and add new vectors
        for(int k = 0,numf = 0; k < M; k++)
            if(schmidt_add(b_p, L, N, f_p[k])) {
                L++;  // <- Increase L if we add one more basis vector
                numf++;
            }

        // If L is close to maxdim, collapse to one guess per root */
        if(maxdim - L < M) {
            if(print) {
                outfile->Printf("Subspace too large: maxdim = %d, L = %d\n", maxdim, L);
                outfile->Printf("Collapsing eigenvectors.\n");
            }
            bnew.zero();
            double** bnew_p = bnew.pointer();
            for(int i = 0; i < M; i++) {
                for(int j = 0; j < L; j++) {
                    for(int k = 0; k < N; k++) {
                        bnew_p[i][k] += alpha_p[j][i] * b_p[j][k];
                    }
                }
            }

            // normalize new vectors
            for(int i = 0; i < M; i++){
                double norm = 0.0;
                for(int k = 0; k < N; k++){
                    norm += bnew_p[i][k] * bnew_p[i][k];
                }
                norm = std::sqrt(norm);
                for(int k = 0; k < N; k++){
                    bnew_p[i][k] = bnew_p[i][k] / norm;
                }
            }
            // Copy them into place
            b.zero();
            for(int k = 0,numf = 0; k < M; k++){
                if(schmidt_add(b_p,k, N, bnew_p[k])) {
                    L++;  // <- Increase L if we add one more basis vector
                    numf++;
                }
            }

            skip_check = true;

            L = M;
        }

        // check convergence on all roots
        if(!skip_check) {
            converged = 0;
            if(print) {
                outfile->Printf("Root      Eigenvalue       Delta  Converged?\n");
                outfile->Printf("---- -------------------- ------- ----------\n");
            }
            for(int k = 0; k < M; k++) {
                double diff = std::fabs(lambda.get(k) - lambda_old.get(k));
                bool this_converged = false;
                if(diff < cutoff) {
                    this_converged = true;
                    converged++;
                }
                lambda_old.set(k,lambda.get(k));
                if(print) {
                    outfile->Printf("%3d  %20.14f %4.3e    %1s\n", k, lambda.get(k), diff,
                           this_converged ? "Y" : "N");
                }
            }
        }

        outfile->Flush();

        iter++;
    }

    /* generate final eigenvalues and eigenvectors */
    //if(converged == M) {
    double** alpha_p = alpha.pointer();
    double** b_p = b.pointer();
    for(int i = 0; i < M; i++) {
        eps[i] = lambda.get(i);
        for(int I = 0; I < N; I++){
            v[I][i] = 0.0;
        }
        for(int j = 0; j < L; j++) {
            for(int I=0; I < N; I++) {
                v[I][i] += alpha_p[j][i] * b_p[j][I];
            }
        }
        // Normalize v
        double norm = 0.0;
        for(int I = 0; I < N; I++) {
            norm += v[I][i] * v[I][i];
        }
        norm = std::sqrt(norm);
        for(int I = 0; I < N; I++) {
            v[I][i] /= norm;
        }
    }
    outfile->Printf("\n  The Davidson-Liu algorithm converged in %d iterations.", iter);
    outfile->Printf("\n  %s: %f s","Time spent diagonalizing H",t_davidson.elapsed());
    return true;
}

}}
