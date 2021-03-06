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

#ifndef _wfn_operator_h_
#define _wfn_operator_h_

#include "determinant_map.h"
#include "stl_bitset_determinant.h"

namespace psi {
namespace forte {

/**
 * @brief A class to compute various expectation values, projections,
 * and matrix elements of quantum mechanical operators on wavefunction objects.
 */

using wfn_hash = det_hash<double>;

class WFNOperator {
  public:
    /// Default constructor
    WFNOperator(std::vector<int> symmetry);

    /// Empty constructor
    WFNOperator();

    /// Initializer
    void initialize(std::vector<int>& symmetry);


    /// Set print level
    void set_quiet_mode( bool mode );

    /// Build the coupling lists for one-particle operators
    void op_lists(DeterminantMap& wfn);
    void op_s_lists(DeterminantMap& wfn);

    /// Build the coupling lists for two-particle operators
    void tp_lists(DeterminantMap& wfn);
    void tp_s_lists(DeterminantMap& wfn);

    /// Build the coupling lists for three-particle operators
    void three_lists(DeterminantMap& wfn);

    void clear_op_lists();
    void clear_tp_lists();

    void clear_op_s_lists();
    void clear_tp_s_lists();
    /*- Operators -*/

    /// Single excitations, a_p^(+) a_q|>
    void add_singles(DeterminantMap& wfn);

    /// Double excitations, a_p^(+) a_q^(+) a_r a_s|>
    void add_doubles(DeterminantMap& wfn);

    /// Compute total spin expectation value <|S^2|>
    double s2(DeterminantMap& wfn, SharedMatrix& evecs, int root);

    void build_strings(DeterminantMap& wfn);    

    /// Build the sparse Hamiltonian
    std::vector<std::pair<std::vector<size_t>, std::vector<double>>> build_H_sparse( const DeterminantMap& wfn);

    /// Build the sparse Hamiltonian -v2
    std::vector<std::pair<std::vector<size_t>, std::vector<double>>> build_H_sparse2( const DeterminantMap& wfn);

    std::vector<std::vector<std::pair<size_t, short>>> a_list_;
    std::vector<std::vector<std::pair<size_t, short>>> b_list_;
    std::vector<std::vector<std::tuple<size_t, short, short>>> aa_list_;
    std::vector<std::vector<std::tuple<size_t, short, short>>> bb_list_;
    std::vector<std::vector<std::tuple<size_t, short, short>>> ab_list_;

    /// The alpha single-annihilation/creation list
    std::vector<std::vector<std::pair<size_t, short>>> a_ann_list_;
    std::vector<std::vector<std::pair<size_t, short>>> a_cre_list_;

    /// The beta single-annihilation/creation list
    std::vector<std::vector<std::pair<size_t, short>>> b_ann_list_;
    std::vector<std::vector<std::pair<size_t, short>>> b_cre_list_;

    /// The alpha-alpha double-annihilation/creation list
    std::vector<std::vector<std::tuple<size_t, short, short>>> aa_ann_list_;
    std::vector<std::vector<std::tuple<size_t, short, short>>> aa_cre_list_;

    /// The beta-beta single-annihilation/creation list
    std::vector<std::vector<std::tuple<size_t, short, short>>> bb_ann_list_;
    std::vector<std::vector<std::tuple<size_t, short, short>>> bb_cre_list_;

    /// The alfa-beta single-annihilation/creation list
    std::vector<std::vector<std::tuple<size_t, short, short>>> ab_ann_list_;
    std::vector<std::vector<std::tuple<size_t, short, short>>> ab_cre_list_;

    /// Three particle lists
    std::vector<std::vector<std::tuple<size_t, short, short, short>>>
        aaa_ann_list_;
    std::vector<std::vector<std::tuple<size_t, short, short, short>>>
        aab_ann_list_;
    std::vector<std::vector<std::tuple<size_t, short, short, short>>>
        abb_ann_list_;
    std::vector<std::vector<std::tuple<size_t, short, short, short>>>
        bbb_ann_list_;

    std::vector<std::vector<std::tuple<size_t, short, short, short>>>
        aaa_cre_list_;
    std::vector<std::vector<std::tuple<size_t, short, short, short>>>
        aab_cre_list_;
    std::vector<std::vector<std::tuple<size_t, short, short, short>>>
        abb_cre_list_;
    std::vector<std::vector<std::tuple<size_t, short, short, short>>>
        bbb_cre_list_;

  protected:
    /// Initialize important variables on construction
    void startup();

    std::vector<std::vector<size_t>> beta_strings_;
    std::vector<std::vector<size_t>> alpha_strings_;
    std::vector<std::vector<std::pair<int,size_t>>> alpha_a_strings_;
    std::vector<std::vector<std::pair<int,size_t>>> beta_a_strings_;
    

    /// Active space symmetry
    std::vector<int> mo_symmetry_;

    /// Print level
    bool quiet_ = false;
};
}
}

#endif // _wfn_operator_h_
