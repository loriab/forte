#ifndef _active_dsrgpt2_h_
#define _active_dsrgpt2_h_

#include <libqt/qt.h>
#include <libpsio/psio.hpp>
#include <libpsio/psio.h>
#include <liboptions/liboptions.h>
#include <libmints/vector.h>
#include <libmints/matrix.h>
#include <libmints/wavefunction.h>
#include <libmints/molecule.h>
#include <vector>
#include <string>

#include "integrals.h"
#include "reference.h"
#include "helpers.h"
#include "fci_mo.h"
#include "stl_bitset_determinant.h"
#include "dsrg_mrpt2.h"
#include "three_dsrg_mrpt2.h"

namespace psi{ namespace forte{
class ACTIVE_DSRGPT2
{
public:
    /**
     * @brief ACTIVE_DSRGPT2 Constructor
     * @param wfn The main wavefunction object
     * @param options PSI4 and FORTE options
     * @param ints ForteInegrals
     * @param mo_space_info MOSpaceInfo
     */
    ACTIVE_DSRGPT2(boost::shared_ptr<Wavefunction> wfn, Options &options,
                   std::shared_ptr<ForteIntegrals> ints, std::shared_ptr<MOSpaceInfo> mo_space_info);

    /// Destructor
    ~ACTIVE_DSRGPT2();

    /// Compute energy
    void compute_energy();

private:
    /// Basic Preparation
    void startup();

    /// The wavefunction pointer
    boost::shared_ptr<Wavefunction> wfn_;

    /// Options
    Options options_;

    /// Integrals
    std::shared_ptr<ForteIntegrals> ints_;

    /// MO space info
    std::shared_ptr<MOSpaceInfo> mo_space_info_;

    /// Total number of roots
    int total_nroots_;

    /// Number of roots per irrep
    std::vector<int> nrootpi_;

    /// Irrep symbol
    std::vector<std::string> irrep_symbol_;

    /// Reference energies
    std::vector<std::vector<double>> ref_energies_;

    /// DSRGPT2 energies
    std::vector<std::vector<double>> pt2_energies_;

    /// Model space of CIS
    std::vector<std::vector<STLBitsetDeterminant>> vecdet_cis_;

    /// Eigen vectors of CIS
    std::vector<std::vector<SharedVector>> eigen_cis_;

    /// Singles (T1) percentage
    std::vector<std::vector<std::pair<int,double>>> t1_percentage_;

    /// Compute the T1%
    void compute_t1_percentage();

    /// Print summary
    void print_summary();
};
}}

#endif // ACTIVE_DSRGPT2_H
