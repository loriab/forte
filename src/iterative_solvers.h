/*
 *@BEGIN LICENSE
 *
 * Libadaptive: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *@END LICENSE
 */

#ifndef _iterative_solvers_h_
#define _iterative_solvers_h_

#include <libqt/qt.h>
#include <libmints/vector.h>
#include <libmints/matrix.h>

namespace psi{ namespace forte{

/**
 * @brief The DavidsonLiuSolver class
 * This class diagonalizes the Hamiltonian in a basis
 * of determinants.
 *
 * Example use:
 *
 *     DavidsonLiuSolver dls(nbasis,nroots);      // create solver
 *     dls.startup(diagonal);                     // initialize solver with diagonal of Matrix
 *     size_t guess_size = dls.collapse_size();   // get the size of the guess
 *
 *     for (size_t n = 0; n < guess_size; ++n){
 *         dls.add_b(guess[n]);                   // add guess vectors
 *     }
 *
 *     bool converged = false;
 *     for (int cycle = 0; cycle < max_cycle; ++cycle){
 *         do{
 *             dls.get_b(b);                      // solver provides a vector b
 *             ...                                // code to compute sigma = Hb
 *             add_sigma = dls.add_sigma(sigma);  // return sigma vector to solver
 *         } while (add_sigma);
 *         converged = dls.update();              // check convergence
 *         if (converged) break;
 *     }
 */
class DavidsonLiuSolver
{
    using sparse_vec = std::vector<std::pair<size_t,double>>;
public:

    // ==> Class Constructor and Destructor <==

    /// Constructor
    DavidsonLiuSolver(size_t size,size_t nroot);

    /// Destructor
    ~DavidsonLiuSolver();

    // ==> Class Interface <==

    /// Set the print level
    void set_print_level(size_t n);
    /// Set the energy convergence
    void set_e_convergence(double value);
    /// Set the residual convergence
    void set_r_convergence(double value);

    /// Return the size of the collapse vectors
    size_t collapse_size() const;

    /// Add a guess basis vector
    void add_guess(SharedVector vec);
    /// Get a basis vector
    void get_b(SharedVector vec);
    /// Add a sigma vector
    bool add_sigma(SharedVector vec);

    void set_project_out(std::vector<sparse_vec> project_out);

    /// Return the eigenvalues
    SharedVector eigenvalues() const;
    /// Return the eigenvectors
    SharedMatrix eigenvectors() const;
    /// Return the n-th eigenvector
    SharedVector eigenvector(size_t n) const;

    /// Initialize the object
    void startup(SharedVector diagonal);

    /// Add a new vector
    bool update();

    ///
    void get_results();

private:

    // ==> Class Private Functions <==

    /// Check that the eigenvectors are orthogonal
    bool check_orthogonality();
    /// Check if the the iterative procedure has converged
    bool check_convergence();
    /// Build the correction vectors
    void form_correction_vectors();
    /// Project out undesired roots
    void project_out_roots(SharedMatrix v);
    /// Normalize the correction vectors
    void normalize_vectors(SharedMatrix v, size_t n);
    /// Perform subspace collapse
    bool subspace_collapse();
    /// Collapse the vectors
    void collapse_vectors();

    // ==> Class Private Data <==

    /// The maximum number of iterations
    int maxiter_ = 35;
    /// The print level
    size_t print_level_ = 1;
    /// Eigenvalue convergence threshold
    double e_convergence_ = 1.0e-15;
    /// Residual convergence threshold
    double r_convergence_ = 1.0e-6;
    /// The dimension of the vectors
    size_t size_;
    /// The number of roots requested
    size_t nroot_;
    /// The number of vectors to retain for each root
    size_t collapse_per_root_ = 2;
    /// The maximum subspace size for each root
    size_t subspace_per_root_ = 4;
    /// The number of vectors to retain after collapse
    size_t collapse_size_;
    /// The maximum subspace size
    size_t subspace_size_;

    int iter_ = 0;
    size_t basis_size_;
    /// The size
    size_t sigma_size_;
    size_t converged_ = 0;
    double timing_ = 0.0;
    bool last_update_collapsed_ = false;

    /// Current set of guess vectors stored by row
    SharedMatrix b_;
    /// Guess vectors formed from old vectors, stored by row
    SharedMatrix bnew;
    /// Residual eigenvectors, stored by row
    SharedMatrix f;
    /// Sigma vectors, stored by column
    SharedMatrix sigma_;
    /// Davidson-Liu mini-Hamitonian
    SharedMatrix G;
    /// Davidson-Liu mini-metric
    SharedMatrix S;
    /// Eigenvectors of the Davidson mini-Hamitonian
    SharedMatrix alpha;

    /// Eigenvalues of the Davidson mini-Hamitonian
    SharedVector lambda;
    /// Old eigenvalues of the Davidson mini-Hamitonian
    SharedVector lambda_old;
    /// Diagonal elements of the Hamiltonian
    SharedVector h_diag;

    /// Approximate eigenstates to project out
    std::vector<sparse_vec> project_out_;
};

}}

#endif // _iterative_solvers_h_
