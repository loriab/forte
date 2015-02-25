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

#ifndef _fcimc_h_
#define _fcimc_h_

#include <fstream>

#include <liboptions/liboptions.h>
#include <libmints/vector.h>
#include <libmints/matrix.h>
#include <libmints/wavefunction.h>
#include <random>


#include "integrals.h"
#include "bitset_determinant.h"

namespace psi{ namespace libadaptive{

typedef std::map<BitsetDeterminant,double> walker_map;

class FCIQMC : public Wavefunction
{
public:
    // ==> Class Constructor and Destructor <==

    /**
     * Constructor
     * @param wfn The main wavefunction object
     * @param options The main options object
     * @param ints A pointer to an allocated integral object
     */
    FCIQMC(boost::shared_ptr<Wavefunction> wfn, Options &options, ExplorerIntegrals* ints);

    /// Destructor
    ~FCIQMC();

    // ==> Class Interface <==

    /// Compute the energy
    double compute_energy();

private:
//    /// The wave function symmetry
//    int wavefunction_symmetry_;
//    /// The symmetry of each orbital in Pitzer ordering
//    std::vector<int> mo_symmetry_;
//    /// The symmetry of each orbital in the qt ordering
//    std::vector<int> mo_symmetry_qt_;
//    /// A vector that contains all the frozen core
//    std::vector<int> frzc_;
//    /// A vector that contains all the frozen virtual
//    std::vector<int> frzv_;
//    /// The nuclear repulsion energy
//    double nuclear_repulsion_energy_;
//    /// The reference determinant
//    StringDeterminant reference_determinant_;
//    /// The determinant with minimum energy
//    StringDeterminant min_energy_determinant_;
//    int compute_pgen(BitsetDeterminant& detI);
    /// The maximum number of threads
    int num_threads_;
    /// Do we have OpenMP?
    static bool have_omp_;
    /// The molecular integrals required by fcimc
    ExplorerIntegrals* ints_;

    /// The wave function symmetry
    int wavefunction_symmetry_;
    /// The symmetry of each orbital in Pitzer ordering
    std::vector<int> mo_symmetry_;
    /// The number of correlated molecular orbitals
    int ncmo_;
    /// The number of correlated molecular orbitals per irrep
    Dimension ncmopi_;
    /// The nuclear repulsion energy
    double nuclear_repulsion_energy_;

    // * Calculation info
    /// The threshold applied to the primary space
    double spawning_threshold_;
    /// The size of the time step (TAU)
    double time_step_;
    /// The maximum number of FCIQMC steps
    size_t maxiter_;

    void startup();
    void print_info();

    // Spawning step
    void spawn(walker_map& walkers,walker_map& new_walkers,double spawning_threshold);
    void singleWalkerSpawn(BitsetDeterminant & new_det, const BitsetDeterminant &det, std::tuple<size_t,size_t,size_t,size_t,size_t> pgen, size_t sumgen);
    // Clone/annihilation step
    void clone_annihilate(walker_map& walkers,walker_map& new_walkers,double spawning_threshold);

    // Death step
    void kill(walker_map& walkers,walker_map& new_walkers,double spawning_threshold);

    // Count the number of allowed single and double excitations
    std::tuple<size_t,size_t,size_t,size_t,size_t> compute_pgen(const BitsetDeterminant &det);
    void compute_excitations(const BitsetDeterminant &det, std::vector<std::tuple<size_t,size_t,size_t,size_t>>& excitations);
    void detExcitation(BitsetDeterminant &new_det, std::tuple<size_t,size_t,size_t,size_t>& rand_ext);

};

}} // End Namespaces

#endif // _fcimc_h_
