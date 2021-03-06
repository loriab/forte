/*
 * @BEGIN LICENSE
 *
 * Forte: an open-source plugin to Psi4 (https://github.com/psi4/psi4)
 * that implements a variety of quantum chemistry methods for strongly
 * correlated electrons.
 *
 * Copyright (c) 2012-2017 by its authors (see LICENSE, AUTHORS).
 *
 * The copyrights for code used from other parties are included in
 * the corresponding files.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 *
 * @END LICENSE
 */

#ifdef HAVE_CHEMPS2

#include "psi4/psi4-dec.h"
#include "psi4/libdpd/dpd.h"
#include "psi4/psifiles.h"
#include "psi4/libpsio/psio.hpp"
#include "psi4/libiwl/iwl.hpp"
#include "psi4/libtrans/integraltransform.h"
#include "psi4/libmints/wavefunction.h"
#include "psi4/libmints/typedefs.h"
// Header above this comment contains typedef std::shared_ptr<psi::Matrix>
// SharedMatrix;
#include "psi4/libciomr/libciomr.h"
#include "psi4/liboptions/liboptions.h"
#include "psi4/libfock/jk.h"
#include "psi4/libmints/writer_file_prefix.h"
// Header above allows to obtain "filename.moleculename" with
// psi::get_writer_file_prefix()

#include <stdlib.h>
#include <iostream>
#include <fstream>

#include "chemps2/Irreps.h"
#include "chemps2/Problem.h"
#include "chemps2/CASSCF.h"
#include "chemps2/Initialize.h"
#include "chemps2/EdmistonRuedenberg.h"

#include "ambit/blocked_tensor.h"
#include "dmrgscf.h"
#include "helpers.h"
#include "integrals/integrals.h"

using namespace std;

// This allows us to be lazy in getting the spaces in DPD calls
#define ID(x) ints->DPD_ID(x)

namespace psi {
namespace forte {

DMRGSCF::DMRGSCF(SharedWavefunction ref_wfn, Options& options,
                 std::shared_ptr<MOSpaceInfo> mo_space_info,
                 std::shared_ptr<ForteIntegrals> ints)
    : Wavefunction(options), mo_space_info_(mo_space_info), ints_(ints) {
    shallow_copy(ref_wfn);
    reference_wavefunction_ = ref_wfn;
    print_method_banner(
        {"Density Matrix Renormalization Group SCF", "Sebastian Wouters"});
    const int dmrg_iterations_ = options_.get_int("DMRGSCF_MAX_ITER");
}

int DMRGSCF::chemps2_groupnumber(const string SymmLabel) {

    int SyGroup = 0;
    bool stopFindGN = false;
    const int magic_number_max_groups_chemps2 = 8;
    do {
        if (SymmLabel.compare(CheMPS2::Irreps::getGroupName(SyGroup)) == 0) {
            stopFindGN = true;
        } else {
            SyGroup++;
        }
    } while ((!stopFindGN) && (SyGroup < magic_number_max_groups_chemps2));

    (*outfile) << "Psi4 symmetry group was found to be <" << SymmLabel.c_str()
               << ">." << endl;
    if (SyGroup >= magic_number_max_groups_chemps2) {
        (*outfile) << "CheMPS2 did not recognize this symmetry group name. "
                      "CheMPS2 only knows:"
                   << endl;
        for (int cnt = 0; cnt < magic_number_max_groups_chemps2; cnt++) {
            (*outfile) << "   <" << (CheMPS2::Irreps::getGroupName(cnt)).c_str()
                       << ">" << endl;
        }
        throw PSIEXCEPTION(
            "CheMPS2 did not recognize the symmetry group name!");
    }
    return SyGroup;
}

void DMRGSCF::buildTmatrix(CheMPS2::DMRGSCFmatrix* theTmatrix,
                           CheMPS2::DMRGSCFindices* iHandler,
                           std::shared_ptr<PSIO> psio, SharedMatrix Cmat) {

    const int nirrep = this->nirrep();
    const int nmo = this->nmo();
    const int nTriMo = nmo * (nmo + 1) / 2;
    const int nso = this->nso();
    const int nTriSo = nso * (nso + 1) / 2;
    int* mopi = this->nmopi();
    int* sopi = this->nsopi();
    double* work1 = new double[nTriSo];
    double* work2 = new double[nTriSo];
    IWL::read_one(psio.get(), PSIF_OEI, PSIF_SO_T, work1, nTriSo, 0, 0,
                  "outfile");
    IWL::read_one(psio.get(), PSIF_OEI, PSIF_SO_V, work2, nTriSo, 0, 0,
                  "outfile");
    for (int n = 0; n < nTriSo; n++) {
        work1[n] += work2[n];
    }
    delete[] work2;

    SharedMatrix soOei;
    soOei = SharedMatrix(new Matrix("SO OEI", nirrep, sopi, sopi));
    SharedMatrix half;
    half = SharedMatrix(new Matrix("Half", nirrep, mopi, sopi));
    SharedMatrix moOei;
    moOei = SharedMatrix(new Matrix("MO OEI", nirrep, mopi, mopi));

    soOei->set(work1);
    half->gemm(true, false, 1.0, Cmat, soOei, 0.0);
    moOei->gemm(false, false, 1.0, half, Cmat, 0.0);
    delete[] work1;

    copyPSIMXtoCHEMPS2MX(moOei, iHandler, theTmatrix);
}

void DMRGSCF::buildJK(SharedMatrix MO_RDM, SharedMatrix MO_JK,
                      SharedMatrix Cmat, std::shared_ptr<JK> myJK) {

    const int nso = this->nso();
    int* nsopi = this->nsopi();
    const int nmo = this->nmo();
    int* nmopi = this->nmopi();
    const int nirrep = this->nirrep();

    // nso can be different from nmo
    SharedMatrix SO_RDM;
    SO_RDM = SharedMatrix(new Matrix("SO RDM", nirrep, nsopi, nsopi));
    SharedMatrix Identity;
    Identity = SharedMatrix(new Matrix("Identity", nirrep, nsopi, nsopi));
    SharedMatrix SO_JK;
    SO_JK = SharedMatrix(new Matrix("SO JK", nirrep, nsopi, nsopi));
    SharedMatrix work;
    work = SharedMatrix(new Matrix("work", nirrep, nsopi, nmopi));

    work->gemm(false, false, 1.0, Cmat, MO_RDM, 0.0);
    SO_RDM->gemm(false, true, 1.0, work, Cmat, 0.0);

    std::vector<SharedMatrix>& CL = myJK->C_left();
    CL.clear();
    CL.push_back(SO_RDM);

    std::vector<SharedMatrix>& CR = myJK->C_right();
    CR.clear();
    Identity->identity();
    CR.push_back(Identity);

    myJK->set_do_J(true);
    myJK->set_do_K(true);
    myJK->set_do_wK(false);
    myJK->compute();

    SO_JK->copy(myJK->K()[0]);
    SO_JK->scale(-0.5);
    SO_JK->add(myJK->J()[0]);

    work->gemm(false, false, 1.0, SO_JK, Cmat, 0.0);
    MO_JK->gemm(true, false, 1.0, Cmat, work, 0.0);
}

void DMRGSCF::copyPSIMXtoCHEMPS2MX(SharedMatrix source,
                                   CheMPS2::DMRGSCFindices* iHandler,
                                   CheMPS2::DMRGSCFmatrix* target) {

    for (int irrep = 0; irrep < iHandler->getNirreps(); irrep++) {
        for (int orb1 = 0; orb1 < iHandler->getNORB(irrep); orb1++) {
            for (int orb2 = 0; orb2 < iHandler->getNORB(irrep); orb2++) {
                target->set(irrep, orb1, orb2, source->get(irrep, orb1, orb2));
            }
        }
    }
}

void DMRGSCF::copyCHEMPS2MXtoPSIMX(CheMPS2::DMRGSCFmatrix* source,
                                   CheMPS2::DMRGSCFindices* iHandler,
                                   SharedMatrix target) {

    for (int irrep = 0; irrep < iHandler->getNirreps(); irrep++) {
        for (int orb1 = 0; orb1 < iHandler->getNORB(irrep); orb1++) {
            for (int orb2 = 0; orb2 < iHandler->getNORB(irrep); orb2++) {
                target->set(irrep, orb1, orb2, source->get(irrep, orb1, orb2));
            }
        }
    }
}

void DMRGSCF::buildQmatOCC(CheMPS2::DMRGSCFmatrix* theQmatOCC,
                           CheMPS2::DMRGSCFindices* iHandler,
                           SharedMatrix MO_RDM, SharedMatrix MO_JK,
                           SharedMatrix Cmat, std::shared_ptr<JK> myJK) {

    MO_RDM->zero();
    for (int irrep = 0; irrep < iHandler->getNirreps(); irrep++) {
        for (int orb = 0; orb < iHandler->getNOCC(irrep); orb++) {
            MO_RDM->set(irrep, orb, orb, 2.0);
        }
    }
    buildJK(MO_RDM, MO_JK, Cmat, myJK);
    copyPSIMXtoCHEMPS2MX(MO_JK, iHandler, theQmatOCC);
}

void DMRGSCF::buildQmatACT(CheMPS2::DMRGSCFmatrix* theQmatACT,
                           CheMPS2::DMRGSCFindices* iHandler, double* DMRG1DM,
                           SharedMatrix MO_RDM, SharedMatrix MO_JK,
                           SharedMatrix Cmat, std::shared_ptr<JK> myJK) {

    MO_RDM->zero();
    const int nOrbDMRG = iHandler->getDMRGcumulative(iHandler->getNirreps());
    for (int irrep = 0; irrep < iHandler->getNirreps(); irrep++) {
        const int NOCC = iHandler->getNOCC(irrep);
        const int shift = iHandler->getDMRGcumulative(irrep);
        for (int orb1 = 0; orb1 < iHandler->getNDMRG(irrep); orb1++) {
            for (int orb2 = orb1; orb2 < iHandler->getNDMRG(irrep); orb2++) {
                const double value =
                    DMRG1DM[shift + orb1 + nOrbDMRG * (shift + orb2)];
                MO_RDM->set(irrep, NOCC + orb1, NOCC + orb2, value);
                MO_RDM->set(irrep, NOCC + orb2, NOCC + orb1, value);
            }
        }
    }
    buildJK(MO_RDM, MO_JK, Cmat, myJK);
    copyPSIMXtoCHEMPS2MX(MO_JK, iHandler, theQmatACT);
}

void DMRGSCF::buildHamDMRG(std::shared_ptr<IntegralTransform> ints,
                           std::shared_ptr<MOSpace> Aorbs_ptr,
                           CheMPS2::DMRGSCFmatrix* theTmatrix,
                           CheMPS2::DMRGSCFmatrix* theQmatOCC,
                           CheMPS2::DMRGSCFindices* iHandler,
                           CheMPS2::Hamiltonian* HamDMRG,
                           std::shared_ptr<PSIO> psio) {

    ints->update_orbitals();
    // Since we don't regenerate the SO ints, we don't call sort_so_tei, and the
    // OEI are not updated !!!!!
    ints->transform_tei(Aorbs_ptr, Aorbs_ptr, Aorbs_ptr, Aorbs_ptr);
    dpd_set_default(ints->get_dpd_id());
    const int nirrep = this->nirrep();

    // Econstant and one-electron integrals
    {
        double Econstant =
            Process::environment.molecule()->nuclear_repulsion_energy();
        for (int h = 0; h < iHandler->getNirreps(); h++) {
            const int NOCC = iHandler->getNOCC(h);
            for (int froz = 0; froz < NOCC; froz++) {
                Econstant += 2 * theTmatrix->get(h, froz, froz) +
                             theQmatOCC->get(h, froz, froz);
            }
            const int shift = iHandler->getDMRGcumulative(h);
            for (int orb1 = 0; orb1 < iHandler->getNDMRG(h); orb1++) {
                for (int orb2 = orb1; orb2 < iHandler->getNDMRG(h); orb2++) {
                    HamDMRG->setTmat(
                        shift + orb1, shift + orb2,
                        theTmatrix->get(h, NOCC + orb1, NOCC + orb2) +
                            theQmatOCC->get(h, NOCC + orb1, NOCC + orb2));
                }
            }
        }
        HamDMRG->setEconst(Econstant);
    }

    // Two-electron integrals
    dpdbuf4 K;
    psio->open(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);
    global_dpd_->buf4_init(&K, PSIF_LIBTRANS_DPD, 0, ID("[S,S]"), ID("[S,S]"),
                           ID("[S>=S]+"), ID("[S>=S]+"), 0, "MO Ints (SS|SS)");
    for (int h = 0; h < nirrep; ++h) {
        global_dpd_->buf4_mat_irrep_init(&K, h);
        global_dpd_->buf4_mat_irrep_rd(&K, h);
        for (int pq = 0; pq < K.params->rowtot[h]; ++pq) {
            const int p = K.params->roworb[h][pq][0];
            const int q = K.params->roworb[h][pq][1];
            for (int rs = 0; rs < K.params->coltot[h]; ++rs) {
                const int r = K.params->colorb[h][rs][0];
                const int s = K.params->colorb[h][rs][1];
                HamDMRG->setVmat(p, r, q, s, K.matrix[h][pq][rs]);
            }
        }
        global_dpd_->buf4_mat_irrep_close(&K, h);
    }
    global_dpd_->buf4_close(&K);
    psio->close(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);
}
void DMRGSCF::buildHamDMRGForte(CheMPS2::DMRGSCFmatrix* theQmatOCC,
                                CheMPS2::DMRGSCFindices* iHandler,
                                CheMPS2::Hamiltonian* HamDMRG,
                                std::shared_ptr<ForteIntegrals> ints) {
    /// Retransform all the integrals for now (TODO:  CASSCF-like integral
    /// transformation)
    ints->retransform_integrals();
    size_t na = mo_space_info_->size("ACTIVE");
    std::vector<size_t> active_orbs = mo_space_info_->get_corr_abs_mo("ACTIVE");
    ambit::Tensor VMat = ints_->aptei_ab_block(active_orbs, active_orbs,
                                               active_orbs, active_orbs);
    for (size_t u = 0; u < na; u++) {
        for (size_t v = 0; v < na; v++) {
            for (size_t x = 0; x < na; x++) {
                for (size_t y = 0; y < na; y++) {
                    size_t offset = u * na * na * na + v * na * na + x * na + y;
                    HamDMRG->setVmat(u, x, v, y, VMat.data()[offset]);
                }
            }
        }
    }
}

void DMRGSCF::fillRotatedTEI_coulomb(std::shared_ptr<IntegralTransform> ints,
                                     std::shared_ptr<MOSpace> OAorbs_ptr,
                                     CheMPS2::DMRGSCFmatrix* theTmatrix,
                                     CheMPS2::DMRGSCFintegrals* theRotatedTEI,
                                     CheMPS2::DMRGSCFindices* iHandler,
                                     std::shared_ptr<PSIO> psio) {

    ints->update_orbitals();
    // Since we don't regenerate the SO ints, we don't call sort_so_tei, and the
    // OEI are not updated !!!!!
    ints->transform_tei(OAorbs_ptr, OAorbs_ptr, MOSpace::all, MOSpace::all);
    dpd_set_default(ints->get_dpd_id());
    const int nirrep = this->nirrep();

    // One-electron integrals
    {
        const int nmo = this->nmo();
        const int nTriMo = nmo * (nmo + 1) / 2;
        const int nso = this->nso();
        const int nTriSo = nso * (nso + 1) / 2;
        int* mopi = this->nmopi();
        int* sopi = this->nsopi();
        double* work1 = new double[nTriSo];
        double* work2 = new double[nTriSo];
        IWL::read_one(psio.get(), PSIF_OEI, PSIF_SO_T, work1, nTriSo, 0, 0,
                      "outfile");
        IWL::read_one(psio.get(), PSIF_OEI, PSIF_SO_V, work2, nTriSo, 0, 0,
                      "outfile");
        for (int n = 0; n < nTriSo; n++) {
            work1[n] += work2[n];
        }
        delete[] work2;

        SharedMatrix soOei;
        soOei = SharedMatrix(new Matrix("SO OEI", nirrep, sopi, sopi));
        SharedMatrix half;
        half = SharedMatrix(new Matrix("Half", nirrep, mopi, sopi));
        SharedMatrix moOei;
        moOei = SharedMatrix(new Matrix("MO OEI", nirrep, mopi, mopi));

        soOei->set(work1);
        half->gemm(true, false, 1.0, this->Ca(), soOei, 0.0);
        moOei->gemm(false, false, 1.0, half, this->Ca(), 0.0);
        delete[] work1;

        copyPSIMXtoCHEMPS2MX(moOei, iHandler, theTmatrix);
    }

    // Two-electron integrals
    dpdbuf4 K;
    psio->open(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);
    // To only process the permutationally unique integrals, change the
    // ID("[A,A]") to ID("[A>=A]+")
    // global_dpd_->buf4_init(&K, PSIF_LIBTRANS_DPD, 0, ID("[A,A]"),
    // ID("[A,A]"), ID("[A>=A]+"), ID("[A>=A]+"), 0, "MO Ints (AA|AA)");
    // int buf4_init(dpdbuf4 *Buf, int inputfile, int irrep, int pqnum, int
    // rsnum, int file_pqnum, int file_rsnum, int anti, const char *label);
    global_dpd_->buf4_init(&K, PSIF_LIBTRANS_DPD, 0, ID("[Q,Q]"), ID("[A,A]"),
                           ID("[Q>=Q]+"), ID("[A>=A]+"), 0, "MO Ints (QQ|AA)");
    for (int h = 0; h < nirrep; ++h) {
        global_dpd_->buf4_mat_irrep_init(&K, h);
        global_dpd_->buf4_mat_irrep_rd(&K, h);
        for (int pq = 0; pq < K.params->rowtot[h]; ++pq) {
            const int p = K.params->roworb[h][pq][0];
            const int q = K.params->roworb[h][pq][1];
            const int psym = K.params->psym[p];
            const int qsym = K.params->qsym[q];
            const int prel = p - K.params->poff[psym];
            const int qrel = q - K.params->qoff[qsym];
            for (int rs = 0; rs < K.params->coltot[h]; ++rs) {
                const int r = K.params->colorb[h][rs][0];
                const int s = K.params->colorb[h][rs][1];
                const int rsym = K.params->rsym[r];
                const int ssym = K.params->ssym[s];
                const int rrel = r - K.params->roff[rsym];
                const int srel = s - K.params->soff[ssym];
                theRotatedTEI->set_coulomb(psym, qsym, rsym, ssym, prel, qrel,
                                           rrel, srel, K.matrix[h][pq][rs]);
            }
        }
        global_dpd_->buf4_mat_irrep_close(&K, h);
    }
    global_dpd_->buf4_close(&K);
    psio->close(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);
}

void DMRGSCF::fillRotatedTEI_exchange(std::shared_ptr<IntegralTransform> ints,
                                      std::shared_ptr<MOSpace> OAorbs_ptr,
                                      std::shared_ptr<MOSpace> Vorbs_ptr,
                                      CheMPS2::DMRGSCFintegrals* theRotatedTEI,
                                      CheMPS2::DMRGSCFindices* iHandler,
                                      std::shared_ptr<PSIO> psio) {

    ints->update_orbitals();
    ints->transform_tei(Vorbs_ptr, OAorbs_ptr, Vorbs_ptr, OAorbs_ptr);
    dpd_set_default(ints->get_dpd_id());

    // Two-electron integrals
    dpdbuf4 K;
    psio->open(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);
    // To only process the permutationally unique integrals, change the
    // ID("[A,A]") to ID("[A>=A]+")
    // global_dpd_->buf4_init(&K, PSIF_LIBTRANS_DPD, 0, ID("[A,A]"),
    // ID("[A,A]"), ID("[A>=A]+"), ID("[A>=A]+"), 0, "MO Ints (AA|AA)");
    // int buf4_init(dpdbuf4 *Buf, int inputfile, int irrep, int pqnum, int
    // rsnum, int file_pqnum, int file_rsnum, int anti, const char *label);
    global_dpd_->buf4_init(&K, PSIF_LIBTRANS_DPD, 0, ID("[T,Q]"), ID("[T,Q]"),
                           ID("[T,Q]"), ID("[T,Q]"), 0, "MO Ints (TQ|TQ)");
    for (int h = 0; h < iHandler->getNirreps(); ++h) {
        global_dpd_->buf4_mat_irrep_init(&K, h);
        global_dpd_->buf4_mat_irrep_rd(&K, h);
        for (int pq = 0; pq < K.params->rowtot[h]; ++pq) {
            const int p = K.params->roworb[h][pq][0];
            const int q = K.params->roworb[h][pq][1];
            const int psym = K.params->psym[p];
            const int qsym = K.params->qsym[q];
            const int prel = p - K.params->poff[psym] +
                             iHandler->getNOCC(psym) + iHandler->getNDMRG(psym);
            const int qrel = q - K.params->qoff[qsym];
            for (int rs = 0; rs < K.params->coltot[h]; ++rs) {
                const int r = K.params->colorb[h][rs][0];
                const int s = K.params->colorb[h][rs][1];
                const int rsym = K.params->rsym[r];
                const int ssym = K.params->ssym[s];
                const int rrel = r - K.params->roff[rsym] +
                                 iHandler->getNOCC(rsym) +
                                 iHandler->getNDMRG(rsym);
                const int srel = s - K.params->soff[ssym];
                theRotatedTEI->set_exchange(qsym, ssym, psym, rsym, qrel, srel,
                                            prel, rrel, K.matrix[h][pq][rs]);
            }
        }
        global_dpd_->buf4_mat_irrep_close(&K, h);
    }
    global_dpd_->buf4_close(&K);
    psio->close(PSIF_LIBTRANS_DPD, PSIO_OPEN_OLD);
}

void DMRGSCF::copyUNITARYtoPSIMX(CheMPS2::DMRGSCFunitary* unitary,
                                 CheMPS2::DMRGSCFindices* iHandler,
                                 SharedMatrix target) {

    for (int irrep = 0; irrep < iHandler->getNirreps(); irrep++) {
        for (int orb1 = 0; orb1 < iHandler->getNORB(irrep); orb1++) {
            for (int orb2 = 0; orb2 < iHandler->getNORB(irrep); orb2++) {
                target->set(irrep, orb1, orb2,
                            unitary->getBlock(
                                irrep)[orb1 + iHandler->getNORB(irrep) * orb2]);
            }
        }
    }
}

void DMRGSCF::update_WFNco(SharedMatrix Coeff_orig,
                           CheMPS2::DMRGSCFindices* iHandler,
                           CheMPS2::DMRGSCFunitary* unitary, SharedMatrix work1,
                           SharedMatrix work2) {

    // copyCHEMPS2MXtoPSIMX( Coeff_orig, iHandler, work1 );
    copyUNITARYtoPSIMX(unitary, iHandler, work2);
    this->Ca()->gemm(false, true, 1.0, Coeff_orig, work2, 0.0);
    this->Cb()->copy(this->Ca());
}

double DMRGSCF::compute_energy() {
    /* This plugin is able to perform a DMRGSCF calculation in a molecular
     * orbital active space. */

    /*******************************
     *   Environment information   *
     *******************************/
    std::shared_ptr<PSIO> psio(_default_psio_lib_); // Grab the global (default)
                                                    // PSIO object, for file I/O
    // std::shared_ptr<Wavefunction> wfn = Process::environment.wavefunction();
    // // The reference (SCF) wavefunction
    if (!reference_wavefunction_) {
        throw PSIEXCEPTION("SCF has not been run yet!");
    }

    /*************************
     *   Fetch the options   *
     *************************/

    const int wfn_irrep = options_.get_int("DMRG_WFN_IRREP");
    const int wfn_multp = options_.get_int("DMRG_WFN_MULTP");
    int* dmrg_states = options_.get_int_array("DMRG_STATES");
    const int ndmrg_states = options_["DMRG_STATES"].size();
    double* dmrg_econv = options_.get_double_array("DMRG_ECONV");
    const int ndmrg_econv = options_["DMRG_ECONV"].size();
    int* dmrg_maxsweeps = options_.get_int_array("DMRG_MAXSWEEPS");
    const int ndmrg_maxsweeps = options_["DMRG_MAXSWEEPS"].size();
    double* dmrg_noiseprefactors =
        options_.get_double_array("DMRG_NOISEPREFACTORS");
    double* dmrg_davidson_tol = options_.get_double_array("DMRG_DAVIDSON_RTOL");
    const int ndmrg_davidson_tol = options_["DMRG_DAVIDSON_RTOL"].size();
    const int ndmrg_noiseprefactors = options_["DMRG_NOISEPREFACTORS"].size();
    const bool dmrg_print_corr = options_.get_bool("DMRG_PRINT_CORR");
    const bool mps_chkpt = options_.get_bool("DMRG_CHKPT");
    // int * frozen_docc                 =
    // options_.get_int_array("FROZEN_DOCC");
    // int * active                      = options_.get_int_array("ACTIVE");
    /// Sebastian optimizes the frozen_docc
    int* frozen_docc = options_.get_int_array("DMRG_FROZEN_DOCC");
    Dimension active = mo_space_info_->get_dimension("ACTIVE");
    const double dmrgscf_convergence = options_.get_double("D_CONVERGENCE");
    const bool dmrgscf_store_unit = options_.get_bool("DMRG_STORE_UNIT");
    const bool dmrgscf_do_diis = options_.get_bool("DMRG_DO_DIIS");
    const double dmrgscf_diis_branch = options_.get_double("DMRG_DIIS_BRANCH");
    const bool dmrgscf_store_diis = options_.get_bool("DMRG_STORE_DIIS");
    const int dmrgscf_which_root = options_.get_int("DMRG_WHICH_ROOT");
    const bool dmrgscf_state_avg = options_.get_bool("DMRG_AVG_STATES");
    const string dmrgscf_active_space = options_.get_str("DMRG_ACTIVE_SPACE");
    const bool dmrgscf_loc_random = options_.get_bool("DMRG_LOC_RANDOM");
    const int dmrgscf_num_vec_diis = CheMPS2::DMRGSCF_numDIISvecs;
    const std::string unitaryname =
        psi::get_writer_file_prefix(this->molecule()->name()) + ".unitary.h5";
    const std::string diisname =
        psi::get_writer_file_prefix(this->molecule()->name()) + ".DIIS.h5";
    bool three_pdm = false;
    if (options_.get_str("JOB_TYPE") == "DSRG-MRPT2" or
        options_.get_str("JOB_TYPE") == "THREE-DSRG-MRPT2") {
        if (options_.get_str("THREEPDC") != "ZERO")
            three_pdm = true;
    }

    /****************************************
     *   Check if the input is consistent   *
     ****************************************/

    const int SyGroup =
        chemps2_groupnumber(Process::environment.molecule()->sym_label());
    const int nmo = this->nmo();
    const int nirrep = this->nirrep();
    int* orbspi = this->nmopi();
    int* docc = this->doccpi();
    int* socc = this->soccpi();
    if (wfn_irrep < 0) {
        throw PSIEXCEPTION(
            "Option WFN_IRREP (integer) may not be smaller than zero!");
    }
    if (wfn_multp < 1) {
        throw PSIEXCEPTION("Option WFN_MULTP (integer) should be larger or "
                           "equal to one: WFN_MULTP = (2S+1) >= 1 !");
    }
    if (ndmrg_states == 0) {
        throw PSIEXCEPTION("Option DMRG_STATES (integer array) should be set!");
    }
    if (ndmrg_econv == 0) {
        throw PSIEXCEPTION("Option DMRG_ECONV (double array) should be set!");
    }
    if (ndmrg_maxsweeps == 0) {
        throw PSIEXCEPTION(
            "Option DMRG_MAXSWEEPS (integer array) should be set!");
    }
    if (ndmrg_noiseprefactors == 0) {
        throw PSIEXCEPTION(
            "Option DMRG_NOISEPREFACTORS (double array) should be set!");
    }
    if (ndmrg_states != ndmrg_econv) {
        throw PSIEXCEPTION("Options DMRG_STATES (integer array) and DMRG_ECONV "
                           "(double array) should contain the same number of "
                           "elements!");
    }
    if (ndmrg_states != ndmrg_maxsweeps) {
        throw PSIEXCEPTION("Options DMRG_STATES (integer array) and "
                           "DMRG_MAXSWEEPS (integer array) should contain the "
                           "same number of elements!");
    }
    if (ndmrg_states != ndmrg_noiseprefactors) {
        throw PSIEXCEPTION("Options DMRG_STATES (integer array) and "
                           "DMRG_NOISEPREFACTORS (double array) should contain "
                           "the same number of elements!");
    }
    for (int cnt = 0; cnt < ndmrg_states; cnt++) {
        if (dmrg_states[cnt] < 2) {
            throw PSIEXCEPTION("Entries in DMRG_STATES (integer array) should "
                               "be larger than 1!");
        }
    }
    if (dmrgscf_convergence <= 0.0) {
        throw PSIEXCEPTION(
            "Option D_CONVERGENCE (double) must be larger than zero!");
    }
    if (dmrgscf_diis_branch <= 0.0) {
        throw PSIEXCEPTION(
            "Option DMRG_DIIS_BRANCH (double) must be larger than zero!");
    }
    if (dmrg_iterations_ < 1) {
        throw PSIEXCEPTION(
            "Option DMRG_MAX_ITER (integer) must be larger than zero!");
    }
    if (dmrgscf_which_root < 1) {
        throw PSIEXCEPTION(
            "Option DMRG_WHICH_ROOT (integer) must be larger than zero!");
    }

    /*******************************************
     *   Create a CheMPS2::ConvergenceScheme   *
     *******************************************/

    CheMPS2::Initialize::Init();
    CheMPS2::ConvergenceScheme* OptScheme =
        new CheMPS2::ConvergenceScheme(ndmrg_states);
    for (int cnt = 0; cnt < ndmrg_states; cnt++) {
        if (ndmrg_davidson_tol != ndmrg_states)
            OptScheme->setInstruction(cnt, dmrg_states[cnt], dmrg_econv[cnt],
                                      dmrg_maxsweeps[cnt],
                                      dmrg_noiseprefactors[cnt]);
        else {
            OptScheme->set_instruction(
                cnt, dmrg_states[cnt], dmrg_econv[cnt], dmrg_maxsweeps[cnt],
                dmrg_noiseprefactors[cnt], dmrg_davidson_tol[cnt]);
        }
    }

    /******************************************************************************
     *   Print orbital information; check consistency of frozen_docc and active
     **
     ******************************************************************************/

    int* nvirtual = new int[nirrep];
    bool virtualsOK = true;
    for (int cnt = 0; cnt < nirrep; cnt++) {
        nvirtual[cnt] = orbspi[cnt] - frozen_docc[cnt] - active[cnt];
        if (nvirtual[cnt] < 0) {
            virtualsOK = false;
        }
    }
    (*outfile) << "wfn_irrep   = " << wfn_irrep << endl;
    (*outfile) << "wfn_multp   = " << wfn_multp << endl;
    (*outfile) << "numOrbitals = [ " << orbspi[0];
    for (int cnt = 1; cnt < nirrep; cnt++) {
        (*outfile) << " , " << orbspi[cnt];
    }
    (*outfile) << " ]" << endl;
    (*outfile) << "R(O)HF DOCC = [ " << docc[0];
    for (int cnt = 1; cnt < nirrep; cnt++) {
        (*outfile) << " , " << docc[cnt];
    }
    (*outfile) << " ]" << endl;
    (*outfile) << "R(O)HF SOCC = [ " << socc[0];
    for (int cnt = 1; cnt < nirrep; cnt++) {
        (*outfile) << " , " << socc[cnt];
    }
    (*outfile) << " ]" << endl;
    (*outfile) << "frozen_docc = [ " << frozen_docc[0];
    for (int cnt = 1; cnt < nirrep; cnt++) {
        (*outfile) << " , " << frozen_docc[cnt];
    }
    (*outfile) << " ]" << endl;
    (*outfile) << "active      = [ " << active[0];
    for (int cnt = 1; cnt < nirrep; cnt++) {
        (*outfile) << " , " << active[cnt];
    }
    (*outfile) << " ]" << endl;
    (*outfile) << "virtual     = [ " << nvirtual[0];
    for (int cnt = 1; cnt < nirrep; cnt++) {
        (*outfile) << " , " << nvirtual[cnt];
    }
    (*outfile) << " ]" << endl;
    if (!virtualsOK) {
        throw PSIEXCEPTION("For at least one irrep: frozen_docc[ irrep ] + "
                           "active[ irrep ] > numOrbitals[ irrep ]!");
    }
    (*outfile) << "DMRGSCF computation run with " << dmrg_iterations_
               << "iterations" << endl;

    /**********************************************
     *   Create another bit of DMRGSCF preamble   *
     **********************************************/
    CheMPS2::DMRGSCFindices* iHandler = new CheMPS2::DMRGSCFindices(
        nmo, SyGroup, frozen_docc, active, nvirtual);
    CheMPS2::DMRGSCFunitary* unitary = new CheMPS2::DMRGSCFunitary(iHandler);
    CheMPS2::DIIS* theDIIS = NULL;
    CheMPS2::DMRGSCFintegrals* theRotatedTEI =
        new CheMPS2::DMRGSCFintegrals(iHandler);
    size_t nOrbDMRG = mo_space_info_->size("ACTIVE");
    double* DMRG1DM = new double[nOrbDMRG * nOrbDMRG];
    double* DMRG2DM = new double[nOrbDMRG * nOrbDMRG * nOrbDMRG * nOrbDMRG];
    double* DMRG3DM;
    if (three_pdm) {
        DMRG3DM = new double[nOrbDMRG * nOrbDMRG * nOrbDMRG * nOrbDMRG *
                             nOrbDMRG * nOrbDMRG];
    }
    CheMPS2::DMRGSCFmatrix* theFmatrix = new CheMPS2::DMRGSCFmatrix(iHandler);
    theFmatrix->clear();
    CheMPS2::DMRGSCFmatrix* theQmatOCC = new CheMPS2::DMRGSCFmatrix(iHandler);
    theQmatOCC->clear();
    CheMPS2::DMRGSCFmatrix* theQmatACT = new CheMPS2::DMRGSCFmatrix(iHandler);
    theQmatACT->clear();
    CheMPS2::DMRGSCFmatrix* theTmatrix = new CheMPS2::DMRGSCFmatrix(iHandler);
    theTmatrix->clear();
    CheMPS2::DMRGSCFwtilde* wmattilde = new CheMPS2::DMRGSCFwtilde(iHandler);
    delete[] nvirtual;

    /***************************************************
     *   Create the active space Hamiltonian storage   *
     ***************************************************/

    int nElectrons = 0;
    for (int cnt = 0; cnt < nirrep; cnt++) {
        nElectrons += 2 * docc[cnt] + socc[cnt];
    }
    (*outfile) << "nElectrons  = " << nElectrons << endl;

    // Number of electrons in the active space
    int nDMRGelectrons = nElectrons;
    for (int cnt = 0; cnt < nirrep; cnt++) {
        nDMRGelectrons -= 2 * frozen_docc[cnt];
    }
    (*outfile) << "nEl. active = " << nDMRGelectrons << endl;

    // Create the CheMPS2::Hamiltonian --> fill later
    int* orbitalIrreps = new int[nOrbDMRG];
    int counterFillOrbitalIrreps = 0;
    for (int h = 0; h < nirrep; h++) {
        for (int cnt = 0; cnt < active[h];
             cnt++) { // Only the active space is treated with DMRG-SCF!
            orbitalIrreps[counterFillOrbitalIrreps] = h;
            counterFillOrbitalIrreps++;
        }
    }
    CheMPS2::Hamiltonian* HamDMRG =
        new CheMPS2::Hamiltonian(nOrbDMRG, SyGroup, orbitalIrreps);
    delete[] orbitalIrreps;

    /* Create the CheMPS2::Problem
       You can fill Ham later, as Problem only keeps a pointer to the
       Hamiltonian object.
       Since only doubly occupied frozen orbitals are allowed, wfn_multp and
       wfn_irrep do not change. */
    CheMPS2::Problem* Prob =
        new CheMPS2::Problem(HamDMRG, wfn_multp - 1, nDMRGelectrons, wfn_irrep);
    if (!(Prob->checkConsistency())) {
        throw PSIEXCEPTION("CheMPS2::Problem : No Hilbert state vector "
                           "compatible with all symmetry sectors!");
    }
    Prob->SetupReorderD2h(); // Does nothing if group not d2h

    /**************************************
     *   Input is parsed and consistent   *
     *   Start with DMRGSCF               *
     **************************************/

    SharedMatrix work1;
    work1 = SharedMatrix(new Matrix("work1", nirrep, orbspi, orbspi));
    SharedMatrix work2;
    work2 = SharedMatrix(new Matrix("work2", nirrep, orbspi, orbspi));
    std::shared_ptr<JK> myJK =
        std::shared_ptr<JK>(new DiskJK(this->basisset(), options_));

    myJK->set_cutoff(0.0);
    myJK->initialize();
    SharedMatrix Coeff_orig = SharedMatrix(new Matrix(this->Ca()));
    // copyPSIMXtoCHEMPS2MX(this->Ca(), iHandler, );

    std::vector<int> OAorbs; // Occupied + active
    std::vector<int> Aorbs;  // Only active
    std::vector<int> Vorbs;  // Virtual
    std::vector<int> empty;
    for (int h = 0; h < iHandler->getNirreps(); h++) {
        for (int orb = 0; orb < iHandler->getNOCC(h) + iHandler->getNDMRG(h);
             orb++) {
            OAorbs.push_back(iHandler->getOrigNOCCstart(h) + orb);
        }
        for (int orb = 0; orb < iHandler->getNDMRG(h); orb++) {
            Aorbs.push_back(iHandler->getOrigNDMRGstart(h) + orb);
        }
        for (int orb = 0; orb < iHandler->getNVIRT(h); orb++) {
            Vorbs.push_back(iHandler->getOrigNVIRTstart(h) + orb);
        }
    }
    std::shared_ptr<MOSpace> OAorbs_ptr;
    OAorbs_ptr = std::shared_ptr<MOSpace>(new MOSpace('Q', OAorbs, empty));
    std::shared_ptr<MOSpace> Aorbs_ptr;
    Aorbs_ptr = std::shared_ptr<MOSpace>(new MOSpace('S', Aorbs, empty));
    std::shared_ptr<MOSpace> Vorbs_ptr;
    Vorbs_ptr = std::shared_ptr<MOSpace>(new MOSpace('T', Vorbs, empty));
    std::vector<std::shared_ptr<MOSpace>> spaces;
    spaces.push_back(OAorbs_ptr);
    spaces.push_back(Aorbs_ptr);
    spaces.push_back(Vorbs_ptr);
    spaces.push_back(MOSpace::all);
    // CheMPS2 requires RHF or ROHF orbitals.
    std::shared_ptr<IntegralTransform> ints;
    ints = std::shared_ptr<IntegralTransform>(new IntegralTransform(
        reference_wavefunction_, spaces, IntegralTransform::Restricted));
    ints->set_keep_iwl_so_ints(true);
    ints->set_keep_dpd_so_ints(true);
    // ints->set_print(6);

    (*outfile) << "###########################################################"
               << endl;
    (*outfile) << "###                                                     ###"
               << endl;
    (*outfile) << "###                       DMRG-SCF                      ###"
               << endl;
    (*outfile) << "###                                                     ###"
               << endl;
    (*outfile) << "###            CheMPS2 by Sebastian Wouters             ###"
               << endl;
    (*outfile) << "###        https://github.com/SebWouters/CheMPS2        ###"
               << endl;
    (*outfile) << "###   Comput. Phys. Commun. 185 (6), 1501-1514 (2014)   ###"
               << endl;
    (*outfile) << "###                                                     ###"
               << endl;
    (*outfile) << "###########################################################"
               << endl;
    (*outfile) << endl;
    (*outfile) << "Number of variables in the x-matrix = "
               << unitary->getNumVariablesX() << endl;

    // Convergence variables
    double gradNorm = 1.0;
    double updateNorm = 1.0;
    double* theupdate = new double[unitary->getNumVariablesX()];
    for (int cnt = 0; cnt < unitary->getNumVariablesX(); cnt++) {
        theupdate[cnt] = 0.0;
    }
    double* theDIISparameterVector = NULL;
    double Energy = 1e8;

    int theDIISvectorParamSize = 0;
    int maxlinsize = 0;
    for (int irrep = 0; irrep < nirrep; irrep++) {
        const int linsize_irrep = iHandler->getNORB(irrep);
        theDIISvectorParamSize += linsize_irrep * (linsize_irrep - 1) / 2;
        if (linsize_irrep > maxlinsize) {
            maxlinsize = linsize_irrep;
        }
    }

    const int nOrbDMRG_pow4 = nOrbDMRG * nOrbDMRG * nOrbDMRG * nOrbDMRG;
    const int unitary_worksize = 4 * maxlinsize * maxlinsize;
    const int sizeWorkMem =
        (nOrbDMRG_pow4 > unitary_worksize) ? nOrbDMRG_pow4 : unitary_worksize;
    double* mem1 = new double[sizeWorkMem];
    double* mem2 = new double[sizeWorkMem];

    CheMPS2::EdmistonRuedenberg* theLocalizer = NULL;
    if (dmrgscf_active_space.compare("LOC") == 0) {
        theLocalizer = new CheMPS2::EdmistonRuedenberg(
            HamDMRG->getVmat(), iHandler->getGroupNumber());
    }

    // Load unitary from disk
    if (dmrgscf_store_unit) {
        struct stat stFileInfo;
        int intStat = stat(unitaryname.c_str(), &stFileInfo);
        if (intStat == 0) {
            unitary->loadU(unitaryname);
        }
    }

    // Load DIIS from disk
    if ((dmrgscf_do_diis) && (dmrgscf_store_diis)) {
        struct stat stFileInfo;
        int intStat = stat(diisname.c_str(), &stFileInfo);
        if (intStat == 0) {
            if (theDIIS == NULL) {
                theDIIS = new CheMPS2::DIIS(theDIISvectorParamSize,
                                            unitary->getNumVariablesX(),
                                            dmrgscf_num_vec_diis);
                theDIISparameterVector = new double[theDIISvectorParamSize];
            }
            theDIIS->loadDIIS(diisname);
        }
    }

    int nIterations = 0;

    /********************************
     ***   Actual DMRGSCF loops   ***
     //********************************/
    while ((gradNorm > dmrgscf_convergence) &&
           (nIterations < dmrg_iterations_)) {

        nIterations++;

        // Update the unitary transformation
        if (unitary->getNumVariablesX() > 0) {

            std::ofstream capturing;
            std::streambuf* cout_buffer;
            string chemps2filename = outfile_name + ".chemps2";
            (*outfile) << "CheMPS2 output is temporarily written to the file "
                       << chemps2filename << " and will be copied here."
                       << endl;
            capturing.open(chemps2filename.c_str(), ios::trunc); // truncate
            cout_buffer = cout.rdbuf(capturing.rdbuf());

            unitary->updateUnitary(mem1, mem2, theupdate, true,
                                   true); // multiply = compact = true
            if ((dmrgscf_do_diis) && (updateNorm <= dmrgscf_diis_branch)) {
                if (dmrgscf_active_space.compare("NO") == 0) {
                    cout << "DIIS has started. Active space not rotated to NOs "
                            "anymore!"
                         << endl;
                }
                if (dmrgscf_active_space.compare("LOC") == 0) {
                    cout << "DIIS has started. Active space not rotated to "
                            "localized orbitals anymore!"
                         << endl;
                }
                if (theDIIS == NULL) {
                    theDIIS = new CheMPS2::DIIS(theDIISvectorParamSize,
                                                unitary->getNumVariablesX(),
                                                dmrgscf_num_vec_diis);
                    theDIISparameterVector = new double[theDIISvectorParamSize];
                    unitary->makeSureAllBlocksDetOne(mem1, mem2);
                }
                unitary->getLog(theDIISparameterVector, mem1, mem2);
                theDIIS->appendNew(theupdate, theDIISparameterVector);
                theDIIS->calculateParam(theDIISparameterVector);
                unitary->updateUnitary(mem1, mem2, theDIISparameterVector,
                                       false,
                                       false); // multiply = compact = false
            }

            cout.rdbuf(cout_buffer);
            capturing.close();
            std::ifstream copying;
            copying.open(chemps2filename, ios::in); // read only
            if (copying.is_open()) {
                string line;
                while (getline(copying, line)) {
                    (*outfile) << line << endl;
                }
                copying.close();
            }
            system(("rm " + chemps2filename).c_str());
        }
        if ((dmrgscf_store_unit) && (gradNorm != 1.0)) {
            unitary->saveU(unitaryname);
        }
        if ((dmrgscf_store_diis) && (updateNorm != 1.0) && (theDIIS != NULL)) {
            theDIIS->saveDIIS(diisname);
        }

        // Fill HamDMRG
        update_WFNco(Coeff_orig, iHandler, unitary, work1, work2);
        buildTmatrix(theTmatrix, iHandler, psio, this->Ca());
        buildQmatOCC(theQmatOCC, iHandler, work1, work2, this->Ca(), myJK);
        buildHamDMRG(ints, Aorbs_ptr, theTmatrix, theQmatOCC, iHandler, HamDMRG,
                     psio);

        // Localize the active space and reorder the orbitals within each irrep
        // based on the exchange matrix
        if ((dmrgscf_active_space.compare("LOC") == 0) &&
            (theDIIS == NULL)) { // When the DIIS has started: stop

            std::ofstream capturing;
            std::streambuf* cout_buffer;
            string chemps2filename = outfile_name + ".chemps2";
            (*outfile) << "CheMPS2 output is temporarily written to the file "
                       << chemps2filename << " and will be copied here."
                       << endl;
            capturing.open(chemps2filename.c_str(), ios::trunc); // truncate
            cout_buffer = cout.rdbuf(capturing.rdbuf());

            theLocalizer->Optimize(mem1, mem2, dmrgscf_loc_random);
            theLocalizer->FiedlerExchange(maxlinsize, mem1, mem2);
            CheMPS2::CASSCF::fillLocalizedOrbitalRotations(
                theLocalizer->getUnitary(), iHandler, mem1);
            unitary->rotateActiveSpaceVectors(mem1, mem2);

            cout.rdbuf(cout_buffer);
            capturing.close();
            std::ifstream copying;
            copying.open(chemps2filename, ios::in); // read only
            if (copying.is_open()) {
                string line;
                while (getline(copying, line)) {
                    (*outfile) << line << endl;
                }
                copying.close();
            }
            system(("rm " + chemps2filename).c_str());

            update_WFNco(Coeff_orig, iHandler, unitary, work1, work2);
            buildTmatrix(theTmatrix, iHandler, psio, this->Ca());
            buildQmatOCC(theQmatOCC, iHandler, work1, work2, this->Ca(), myJK);
            buildHamDMRG(ints, Aorbs_ptr, theTmatrix, theQmatOCC, iHandler,
                         HamDMRG, psio);
            (*outfile) << "Rotated the active space to localized orbitals, "
                          "sorted according to the exchange matrix."
                       << endl;
        }

        // Do the DMRG sweeps, and calculate the 2DM
        {
            std::ofstream capturing;
            std::streambuf* cout_buffer;
            string chemps2filename = outfile_name + ".chemps2";
            (*outfile) << "CheMPS2 output is temporarily written to the file "
                       << chemps2filename << " and will be copied here."
                       << endl;
            capturing.open(chemps2filename.c_str(), ios::trunc); // truncate
            cout_buffer = cout.rdbuf(capturing.rdbuf());

            for (int cnt = 0; cnt < nOrbDMRG_pow4; cnt++) {
                DMRG2DM[cnt] = 0.0;
            } // Clear the 2-RDM (to allow for state-averaged calculations)
            const string psi4TMPpath =
                PSIOManager::shared_object()->get_default_path();
            CheMPS2::DMRG* theDMRG =
                new CheMPS2::DMRG(Prob, OptScheme, mps_chkpt, psi4TMPpath);
            for (int state = 0; state < dmrgscf_which_root; state++) {
                if (state > 0) {
                    theDMRG->newExcitation(fabs(Energy));
                }
                Energy = theDMRG->Solve();
                if (dmrgscf_state_avg) { // When SA-DMRGSCF: 2DM += current 2DM
                    // theDMRG->calc2DMandCorrelations();
                    theDMRG->calc_rdms_and_correlations(three_pdm);
                    CheMPS2::CASSCF::copy2DMover(theDMRG->get2DM(), nOrbDMRG,
                                                 DMRG2DM);
                }
                if ((state == 0) && (dmrgscf_which_root > 1)) {
                    theDMRG->activateExcitations(dmrgscf_which_root - 1);
                }
            }
            if (!(dmrgscf_state_avg)) { // When SS-DMRGSCF: 2DM += last 2DM
                // theDMRG->calc2DMandCorrelations();
                theDMRG->calc_rdms_and_correlations(three_pdm);
                CheMPS2::CASSCF::copy2DMover(theDMRG->get2DM(), nOrbDMRG,
                                             DMRG2DM);
            }
            if (dmrg_print_corr) {
                theDMRG->getCorrelations()->Print();
            }
            if (CheMPS2::DMRG_storeRenormOptrOnDisk) {
                theDMRG->deleteStoredOperators();
            }
            if ((dmrgscf_state_avg) && (dmrgscf_which_root > 1)) {
                const double averagingfactor = 1.0 / dmrgscf_which_root;
                for (int cnt = 0; cnt < nOrbDMRG_pow4; cnt++) {
                    DMRG2DM[cnt] *= averagingfactor;
                }
            }
            CheMPS2::CASSCF::setDMRG1DM(nDMRGelectrons, nOrbDMRG, DMRG1DM,
                                        DMRG2DM);
            // CheMPS2::CASSCF::calcNOON( iHandler, mem1, mem2, DMRG1DM );
            if (three_pdm) {
                theDMRG->get3DM()->fill_ham_index(1.0, false, DMRG3DM, 0,
                                                  nOrbDMRG);
            }
            delete theDMRG;

            cout.rdbuf(cout_buffer);
            capturing.close();
            std::ifstream copying;
            copying.open(chemps2filename, ios::in); // read only
            if (copying.is_open()) {
                string line;
                while (getline(copying, line)) {
                    (*outfile) << line << endl;
                }
                copying.close();
            }
            system(("rm " + chemps2filename).c_str());
        }

        bool wfn_co_updated = false;
        if ((dmrgscf_active_space.compare("NO") == 0) &&
            (theDIIS == NULL)) { // When the DIIS has started: stop
            CheMPS2::CASSCF::copy_active(DMRG1DM, theFmatrix, iHandler, true);
            CheMPS2::CASSCF::block_diagonalize('A', theFmatrix, unitary, mem1,
                                               mem2, iHandler, true, DMRG2DM,
                                               nullptr, nullptr);
            CheMPS2::CASSCF::setDMRG1DM(nDMRGelectrons, nOrbDMRG, DMRG1DM,
                                        DMRG2DM);
            update_WFNco(Coeff_orig, iHandler, unitary, work1, work2);
            wfn_co_updated = true;
            buildTmatrix(theTmatrix, iHandler, psio, this->Ca());
            buildQmatOCC(theQmatOCC, iHandler, work1, work2, this->Ca(), myJK);
            (*outfile) << "Rotated the active space to natural orbitals, "
                          "sorted according to the NOON."
                       << endl;
        }

        if (dmrg_iterations_ == nIterations) {
            if (dmrgscf_store_unit) {
                unitary->saveU(unitaryname);
            }
            break;
        }

        if (!wfn_co_updated) {
            update_WFNco(Coeff_orig, iHandler, unitary, work1, work2);
        }
        buildQmatACT(theQmatACT, iHandler, DMRG1DM, work1, work2, this->Ca(),
                     myJK);
        fillRotatedTEI_coulomb(ints, OAorbs_ptr, theTmatrix, theRotatedTEI,
                               iHandler, psio); // Also fills the T-matrix
        fillRotatedTEI_exchange(ints, OAorbs_ptr, Vorbs_ptr, theRotatedTEI,
                                iHandler, psio);

        {
            std::ofstream capturing;
            std::streambuf* cout_buffer;
            string chemps2filename = outfile_name + ".chemps2";
            (*outfile) << "CheMPS2 output is temporarily written to the file "
                       << chemps2filename << " and will be copied here."
                       << endl;
            capturing.open(chemps2filename.c_str(), ios::trunc); // truncate
            cout_buffer = cout.rdbuf(capturing.rdbuf());

            CheMPS2::CASSCF::buildFmat(theFmatrix, theTmatrix, theQmatOCC,
                                       theQmatACT, iHandler, theRotatedTEI,
                                       DMRG2DM, DMRG1DM);
            CheMPS2::CASSCF::buildWtilde(wmattilde, theTmatrix, theQmatOCC,
                                         theQmatACT, iHandler, theRotatedTEI,
                                         DMRG2DM, DMRG1DM);
            CheMPS2::CASSCF::augmentedHessianNR(theFmatrix, wmattilde, iHandler,
                                                unitary, theupdate, &updateNorm,
                                                &gradNorm);

            cout.rdbuf(cout_buffer);
            capturing.close();
            std::ifstream copying;
            copying.open(chemps2filename, ios::in); // read only
            if (copying.is_open()) {
                string line;
                while (getline(copying, line)) {
                    (*outfile) << line << endl;
                }
                copying.close();
            }
            system(("rm " + chemps2filename).c_str());
        }
    }
    compute_reference(DMRG1DM, DMRG2DM, DMRG3DM, iHandler);
    for (int i = 0; i < nOrbDMRG; i++)
        for (int j = 0; j < nOrbDMRG; j++)

            delete[] mem1;
    delete[] mem2;
    delete[] theupdate;
    if (theDIISparameterVector != NULL) {
        delete[] theDIISparameterVector;
    }
    if (theLocalizer != NULL) {
        delete theLocalizer;
    }
    if (theDIIS != NULL) {
        delete theDIIS;
    }

    delete wmattilde;
    delete theTmatrix;
    delete theQmatOCC;
    delete theQmatACT;
    delete theFmatrix;
    delete[] DMRG1DM;
    delete[] DMRG2DM;
    if (three_pdm) {
        delete[] DMRG3DM;
    }
    delete theRotatedTEI;
    delete unitary;
    delete iHandler;

    delete OptScheme;
    delete Prob;
    delete HamDMRG;

    outfile->Printf("The DMRG-SCF energy = %3.10f \n", Energy);
    Process::environment.globals["CURRENT ENERGY"] = Energy;
    Process::environment.globals["DMRGSCF ENERGY"] = Energy;
    dmrg_ref_.set_Eref(Energy);
    return Energy;
}
void DMRGSCF::compute_reference(double* one_rdm, double* two_rdm,
                                double* three_rdm,
                                CheMPS2::DMRGSCFindices* iHandler) {
    // if(options_.get_int("MULTIPLICITY") != 1 &&
    // options_.get_int("DMRG_WFN_MULTP") != 1)
    //{
    //    outfile->Printf("\n\n Spinadapted formalism requires spin-averaged
    //    quantitities");
    //    throw PSIEXCEPTION("You need to spin averaged things");
    //}
    Reference dmrg_ref;
    size_t na = mo_space_info_->size("ACTIVE");
    ambit::Tensor gamma1_a =
        ambit::Tensor::build(ambit::CoreTensor, "gamma1_a", {na, na});
    ambit::Tensor gamma2_dmrg = ambit::Tensor::build(
        ambit::CoreTensor, "Gamma2_DMRG", {na, na, na, na});
    ambit::Tensor gamma2_aa =
        ambit::Tensor::build(ambit::CoreTensor, "gamma2_aa", {na, na, na, na});
    ambit::Tensor gamma2_ab =
        ambit::Tensor::build(ambit::CoreTensor, "gamma2_ab", {na, na, na, na});

    const int nOrbDMRG = iHandler->getDMRGcumulative(iHandler->getNirreps());
    std::vector<double>& gamma1_data = gamma1_a.data();
    for (int irrep = 0; irrep < iHandler->getNirreps(); irrep++) {
        const int shift = iHandler->getDMRGcumulative(irrep);
        for (int orb1 = 0; orb1 < iHandler->getNDMRG(irrep); orb1++) {
            for (int orb2 = orb1; orb2 < iHandler->getNDMRG(irrep); orb2++) {
                const double value =
                    one_rdm[shift + orb1 + nOrbDMRG * (shift + orb2)];
                gamma1_data[shift + orb1 + nOrbDMRG * (shift + orb2)] =
                    0.5 * value;
                gamma1_data[shift + orb2 + nOrbDMRG * (shift + orb1)] =
                    0.5 * value;
            }
        }
    }
    /// Gamma_a = 1_RDM / 2
    /// Gamma_b = 1_RDM / 2
    dmrg_ref.set_L1a(gamma1_a);
    dmrg_ref.set_L1b(gamma1_a);
    /// Form 2_rdms
    {
        gamma2_dmrg.iterate([&](const ::vector<size_t>& i, double& value) {
            value = two_rdm[i[0] * na * na * na + i[1] * na * na + i[2] * na +
                            i[3]];
        });
        /// gamma2_aa = 1 / 6 * (Gamma2(pqrs) - Gamma2(pqsr))
        gamma2_aa.copy(gamma2_dmrg);
        gamma2_aa("p, q, r, s") =
            gamma2_dmrg("p, q, r, s") - gamma2_dmrg("p, q, s, r");
        gamma2_aa.scale(1.0 / 6.0);

        gamma2_ab("p, q, r, s") =
            (2.0 * gamma2_dmrg("p, q, r, s") + gamma2_dmrg("p, q, s, r"));
        gamma2_ab.scale(1.0 / 6.0);
        dmrg_ref.set_g2aa(gamma2_aa);
        dmrg_ref.set_g2bb(gamma2_aa);
        dmrg_ref.set_g2ab(gamma2_ab);
        ambit::Tensor cumulant2_aa = ambit::Tensor::build(
            ambit::CoreTensor, "Cumulant2_aa", {na, na, na, na});
        ambit::Tensor cumulant2_ab = ambit::Tensor::build(
            ambit::CoreTensor, "Cumulant2_ab", {na, na, na, na});
        cumulant2_aa.copy(gamma2_aa);
        cumulant2_aa("pqrs") -= gamma1_a("pr") * gamma1_a("qs");
        cumulant2_aa("pqrs") += gamma1_a("ps") * gamma1_a("qr");

        cumulant2_ab.copy(gamma2_ab);
        cumulant2_ab("pqrs") -= gamma1_a("pr") * gamma1_a("qs");
        dmrg_ref.set_L2aa(cumulant2_aa);
        dmrg_ref.set_L2ab(cumulant2_ab);
        dmrg_ref.set_L2bb(cumulant2_aa);
    }
    if ((options_.get_str("THREEPDC") != "ZERO") &&
        (options_.get_str("JOB_TYPE") == "DSRG-MRPT2" or
         options_.get_str("JOB_TYPE") == "THREE-DSRG-MRPT2")) {
        ambit::Tensor gamma3_dmrg = ambit::Tensor::build(
            ambit::CoreTensor, "Gamma3_DMRG", {na, na, na, na, na, na});
        ambit::Tensor gamma3_aaa = ambit::Tensor::build(
            ambit::CoreTensor, "Gamma3_aaa", {na, na, na, na, na, na});
        ambit::Tensor gamma3_aab = ambit::Tensor::build(
            ambit::CoreTensor, "Gamma3_aab", {na, na, na, na, na, na});
        ambit::Tensor gamma3_abb = ambit::Tensor::build(
            ambit::CoreTensor, "Gamma2_abb", {na, na, na, na, na, na});
        gamma3_dmrg.iterate([&](const ::vector<size_t>& i, double& value) {
            value = three_rdm[i[0] * na * na * na * na * na +
                              i[1] * na * na * na * na + i[2] * na * na * na +
                              i[3] * na * na + i[4] * na + i[5]];
        });
        gamma3_aaa("p, q, r, s, t, u") = gamma3_dmrg("p, q, r, s, t, u") +
                                         gamma3_dmrg("p, q, r, t, u, s") +
                                         gamma3_dmrg("p, q, r, u, s, t");
        gamma3_aaa.scale(1.0 / 12.0);
        gamma3_aab("p, q, r, s, t, u") =
            (gamma3_dmrg("p, q, r, s, t, u") - gamma3_dmrg("p, q, r, t, u, s") -
             gamma3_dmrg("p, q, r, u, s, t") -
             2.0 * gamma3_dmrg("p, q, r, t, s, u"));
        gamma3_aab.scale(1.0 / 12.0);
        // gamma3_abb("p, q, r, s, t, u") = (gamma3_dmrg("p, q, r, s, t, u") -
        // gamma3_dmrg("p, q, r, t, u, s") - gamma3_dmrg("p, q, r, u, s, t") -
        // 2.0 * gamma3_dmrg("p, q, r, t, s, u"));
        // gamma3_abb.scale(1.0/12.0);

        ambit::Tensor L1a = dmrg_ref.L1a();
        ambit::Tensor L1b = dmrg_ref.L1b();
        ambit::Tensor L2aa = dmrg_ref.L2aa();
        ambit::Tensor L2ab = dmrg_ref.L2ab();
        // ambit::Tensor L2bb = dmrg_ref.L2bb();
        // Convert the 3-RDMs to 3-RCMs
        gamma3_aaa("pqrstu") -= L1a("ps") * L2aa("qrtu");
        gamma3_aaa("pqrstu") += L1a("pt") * L2aa("qrsu");
        gamma3_aaa("pqrstu") += L1a("pu") * L2aa("qrts");

        gamma3_aaa("pqrstu") -= L1a("qt") * L2aa("prsu");
        gamma3_aaa("pqrstu") += L1a("qs") * L2aa("prtu");
        gamma3_aaa("pqrstu") += L1a("qu") * L2aa("prst");

        gamma3_aaa("pqrstu") -= L1a("ru") * L2aa("pqst");
        gamma3_aaa("pqrstu") += L1a("rs") * L2aa("pqut");
        gamma3_aaa("pqrstu") += L1a("rt") * L2aa("pqsu");

        gamma3_aaa("pqrstu") -= L1a("ps") * L1a("qt") * L1a("ru");
        gamma3_aaa("pqrstu") -= L1a("pt") * L1a("qu") * L1a("rs");
        gamma3_aaa("pqrstu") -= L1a("pu") * L1a("qs") * L1a("rt");

        gamma3_aaa("pqrstu") += L1a("ps") * L1a("qu") * L1a("rt");
        gamma3_aaa("pqrstu") += L1a("pu") * L1a("qt") * L1a("rs");
        gamma3_aaa("pqrstu") += L1a("pt") * L1a("qs") * L1a("ru");

        gamma3_aab("pqRstU") -= L1a("ps") * L2ab("qRtU");
        gamma3_aab("pqRstU") += L1a("pt") * L2ab("qRsU");

        gamma3_aab("pqRstU") -= L1a("qt") * L2ab("pRsU");
        gamma3_aab("pqRstU") += L1a("qs") * L2ab("pRtU");

        gamma3_aab("pqRstU") -= L1b("RU") * L2aa("pqst");

        gamma3_aab("pqRstU") -= L1a("ps") * L1a("qt") * L1b("RU");
        gamma3_aab("pqRstU") += L1a("pt") * L1a("qs") * L1b("RU");

        // gamma3_abb("pQRsTU") -= L1a("ps") * L2aa("QRTU");

        // gamma3_abb("pQRsTU") -= L1b("QT") * L2ab("pRsU");
        // gamma3_abb("pQRsTU") += L1b("QU") * L2ab("pRsT");

        // gamma3_abb("pQRsTU") -= L1b("RU") * L2ab("pQsT");
        // gamma3_abb("pQRsTU") += L1b("RT") * L2ab("pQsU");

        // gamma3_abb("pQRsTU") -= L1a("ps") * L1b("QT") * L1b("RU");
        // gamma3_abb("pQRsTU") += L1a("ps") * L1b("QU") * L1b("RT");
        gamma3_abb("p, q, r, s, t, u") = gamma3_aab("q,r,p,t,u,s");

        dmrg_ref.set_L3aaa(gamma3_aaa);
        dmrg_ref.set_L3aab(gamma3_aab);
        dmrg_ref.set_L3abb(gamma3_abb);
        dmrg_ref.set_L3bbb(gamma3_aaa);
    }
    dmrg_ref_ = dmrg_ref;
}
}
} // End Namespaces

#endif // #ifdef HAVE_CHEMPS2
