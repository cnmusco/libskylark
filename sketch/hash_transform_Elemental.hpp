#ifndef SKYLARK_HASH_TRANSFORM_ELEMENTAL_HPP
#define SKYLARK_HASH_TRANSFORM_ELEMENTAL_HPP

#include <elemental.hpp>

#include "context.hpp"
#include "hash_transform_data.hpp"
#include "transforms.hpp"

namespace skylark { namespace sketch {

template <typename ValueType,
          elem::Distribution ColDist,
          typename IdxDistributionType,
          template <typename> class ValueDistributionType>
struct hash_transform_t <
  elem::DistMatrix<ValueType, ColDist, elem::STAR>,
  elem::Matrix<ValueType>,
  IdxDistributionType,
  ValueDistributionType > :
  public hash_transform_data_t<int, 
                               ValueType, 
                               IdxDistributionType, 
                               ValueDistributionType> {
  // Typedef matrix type so that we can use it regularly
  typedef ValueType value_type;
  typedef elem::DistMatrix<value_type, ColDist, elem::STAR> matrix_type;
  typedef elem::Matrix<value_type> output_matrix_type;
  typedef IdxDistributionType idx_distribution_type;
  typedef ValueDistributionType<value_type> value_distribution_type;
  typedef hash_transform_data_t<int, 
                                ValueType, 
                                IdxDistributionType, 
                                ValueDistributionType> base_data_t;

  /**
   * Constructor
   * Create an object with a particular seed value.
   */
  hash_transform_t (int N, int S, skylark::sketch::context_t& context) :
      base_data_t (N, S, context) {}
 
  template <typename InputMatrixType,
        typename OutputMatrixType>
  hash_transform_t (hash_transform_t<InputMatrixType, 
                     OutputMatrixType,
                     IdxDistributionType,
                     ValueDistributionType>& other) : 
        base_data_t(other.get_data()) {}

  /**
   * Apply the sketching transform that is described in by the sketch_of_A.
   */
  template <typename Dimension>
  void apply (const matrix_type& A, output_matrix_type& sketch_of_A,
        Dimension dimension) {

    switch(ColDist) {
    case elem::VR:
    case elem::VC:
      apply_impl_vdist (A, sketch_of_A, dimension);
      break;

    default:
      std::cerr << "Unsupported for now..." << std::endl;
      break;
    }
  }

private:
  /**
   * Apply the sketching transform that is described in by the sketch_of_A.
   * Implementation for the column-wise direction of sketching.
   */
  void apply_impl_vdist (const matrix_type& A,
               output_matrix_type& sketch_of_A,
               skylark::sketch::columnwise_tag) {

    // Create space to hold local part of SA
    elem::Matrix<value_type> SA_part (sketch_of_A.Height(),
                      sketch_of_A.Width(),
                      sketch_of_A.LDim());

    //XXX: newly created matrix is not zeroed!
    elem::Zero(SA_part);

    // Construct Pi * A (directly on the fly)
    for (size_t j = 0; j < A.LocalHeight(); j++) {

      size_t col_idx = A.ColShift() + A.ColStride() * j;

      size_t row_idx      = base_data_t::row_idx[col_idx];
      value_type scale_factor = base_data_t::row_value[col_idx];

      for(size_t i = 0; i < A.LocalWidth(); i++) {
        value_type value = scale_factor * A.GetLocal(j, i);
        SA_part.Update(row_idx, A.RowShift() + i, value);
      }
    }

    // Pull everything to rank-0
    boost::mpi::reduce (base_data_t::context.comm,
              SA_part.LockedBuffer(),
              SA_part.MemorySize(),
              sketch_of_A.Buffer(),
              std::plus<value_type>(),
              0);
  }

  /**
   * Apply the sketching transform that is described in by the sketch_of_A.
   * Implementation for the row-wise direction of sketching.
   */
  void apply_impl_vdist (const matrix_type& A,
               output_matrix_type& sketch_of_A,
               skylark::sketch::rowwise_tag) {

    // Create space to hold local part of SA
    elem::Matrix<value_type> SA_part (sketch_of_A.Height(),
                      sketch_of_A.Width(),
                      sketch_of_A.LDim());

    elem::Zero(SA_part);

    // Construct A * Pi (directly on the fly)
    for (size_t j = 0; j < A.LocalHeight(); ++j) {

      size_t row_idx = A.ColShift() + A.ColStride() * j;

      for(size_t i = 0; i < A.LocalWidth(); ++i) {

        size_t col_idx   = A.RowShift() + A.RowStride() * i;
        size_t new_col_idx = base_data_t::row_idx[col_idx];
        value_type value   = 
            base_data_t::row_value[col_idx] * A.GetLocal(j, i);

        SA_part.Update(row_idx, new_col_idx, value);
      }
    }

    // Pull everything to rank-0
    boost::mpi::reduce (base_data_t::context.comm,
              SA_part.LockedBuffer(),
              SA_part.MemorySize(),
              sketch_of_A.Buffer(),
              std::plus<value_type>(),
              0);
  }
};

} } /** namespace skylark::sketch */

#endif // SKYLARK_HASH_TRANSFORM_ELEMENTAL_HPP