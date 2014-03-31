#ifndef SKYLARK_HASH_TRANSFORM_LOCAL_SPARSE_HPP
#define SKYLARK_HASH_TRANSFORM_LOCAL_SPARSE_HPP

#include <boost/dynamic_bitset.hpp>

#include "../base/sparse_matrix.hpp"
#include "../utility/exception.hpp"

#include "context.hpp"
#include "transforms.hpp"
#include "hash_transform_data.hpp"


namespace skylark { namespace sketch {

/* Specialization: local SpMat for input, output */
template <typename ValueType,
          template <typename> class IdxDistributionType,
          template <typename> class ValueDistribution>
struct hash_transform_t <
    base::sparse_matrix_t<ValueType>,
    base::sparse_matrix_t<ValueType>,
    IdxDistributionType,
    ValueDistribution > :
        public hash_transform_data_t<size_t,
                                     ValueType,
                                     IdxDistributionType,
                                     ValueDistribution>,
        virtual public sketch_transform_t<base::sparse_matrix_t<ValueType>,
                                          base::sparse_matrix_t<ValueType> >  {
    typedef size_t index_type;
    typedef ValueType value_type;
    typedef base::sparse_matrix_t<ValueType> matrix_type;
    typedef base::sparse_matrix_t<ValueType> output_matrix_type;
    typedef IdxDistributionType<index_type> idx_distribution_type;
    typedef ValueDistribution<value_type> value_distribution_type;
    typedef hash_transform_data_t<index_type,
                                  value_type,
                                  IdxDistributionType,
                                  ValueDistribution> base_data_t;


    /**
     * Regular constructor
     */
    hash_transform_t (int N, int S, skylark::sketch::context_t& context) :
        base_data_t(N, S, context) {}

    /**
     * Copy constructor
     */
    template <typename InputMatrixType,
              typename OutputMatrixType>
    hash_transform_t (hash_transform_t<InputMatrixType,
                                       OutputMatrixType,
                                       IdxDistributionType,
                                       ValueDistribution>& other) :
        base_data_t(other) {}

    /**
     * Constructor from data
     */
    hash_transform_t (hash_transform_data_t<index_type,
                                            value_type,
                                            IdxDistributionType,
                                            ValueDistribution>& other_data) :
        base_data_t(other_data) {}

    /**
     * Apply columnwise the sketching transform that is described by the
     * the transform with output sketch_of_A.
     */
    void apply (const matrix_type &A, output_matrix_type &sketch_of_A,
                columnwise_tag dimension) const {
        try {
            apply_impl (A, sketch_of_A, dimension);
        } catch(boost::mpi::exception e) {
            SKYLARK_THROW_EXCEPTION (
                utility::mpi_exception()
                    << utility::error_msg(e.what()) );
        } catch (std::string e) {
            SKYLARK_THROW_EXCEPTION (
                utility::combblas_exception()
                    << utility::error_msg(e) );
        } catch (std::logic_error e) {
            SKYLARK_THROW_EXCEPTION (
                utility::combblas_exception()
                    << utility::error_msg(e.what()) );
        }
    }

    /**
     * Apply rowwise the sketching transform that is described by the
     * the transform with output sketch_of_A.
     */
    void apply (const matrix_type &A, output_matrix_type &sketch_of_A,
                rowwise_tag dimension) const {
        try {
            apply_impl (A, sketch_of_A, dimension);
        } catch(boost::mpi::exception e) {
            SKYLARK_THROW_EXCEPTION (
                utility::mpi_exception()
                    << utility::error_msg(e.what()) );
        } catch (std::string e) {
            SKYLARK_THROW_EXCEPTION (
                utility::combblas_exception()
                    << utility::error_msg(e) );
        } catch (std::logic_error e) {
            SKYLARK_THROW_EXCEPTION (
                utility::combblas_exception()
                    << utility::error_msg(e.what()) );
        }
    }

    int get_N() const { return this->_N; } /**< Get input dimension. */
    int get_S() const { return this->_S; } /**< Get output dimension. */

private:
    /**
     * Apply the sketching transform that is described in by the sketch_of_A
     * columnwise.
     */
    void apply_impl (const matrix_type &A,
                     output_matrix_type &sketch_of_A,
                     columnwise_tag) const {

        index_type col_idx = 0;

        const int* indptr  = A.indptr();
        const int* indices = A.indices();
        const value_type* values = A.locked_values();

        index_type n_rows = base_data_t::_S;
        index_type n_cols = A.width();

        int nnz = 0;
        int *indptr_new = new int[n_cols + 1];
        std::vector<int> final_rows(A.nonzeros());
        std::vector<value_type> final_vals(A.nonzeros());

        indptr_new[0] = 0;
        std::vector<index_type> idx_map(n_rows, -1);

        for(index_type col = 0; col < A.width(); col++) {


            for(index_type idx = indptr[col]; idx < indptr[col + 1]; idx++) {

                index_type row = indices[idx];
                value_type val = values[idx] * base_data_t::row_value[row];
                row            = base_data_t::row_idx[row];

                //XXX: I think we should get rid of the if here...
                if(idx_map[row] == -1) {
                    idx_map[row] = nnz;
                    final_rows[nnz] = row;
                    final_vals[nnz] = val;
                    nnz++;
                } else {
                    final_vals[idx_map[row]] += val;
                }
            }

            indptr_new[col + 1] = nnz;

            // reset idx_map
            for(int i = indptr_new[col]; i < nnz; ++i)
                idx_map[final_rows[i]] = -1;
        }

        int *indices_new = new int[nnz];
        std::copy(final_rows.begin(), final_rows.begin() + nnz, indices_new);

        double *values_new = new double[nnz];
        std::copy(final_vals.begin(), final_vals.begin() + nnz, values_new);

        // let the sparse structure take ownership of the data
        sketch_of_A.attach(indptr_new, indices_new, values_new,
                           nnz, n_rows, n_cols, true);
    }


    /**
     * Apply the sketching transform that is described in by the sketch_of_A
     * rowwise.
     */
    void apply_impl (const matrix_type &A,
                     output_matrix_type &sketch_of_A,
                     rowwise_tag) const {

        index_type col_idx = 0;

        const int* indptr = A.indptr();
        const int* indices = A.indices();
        const value_type* values = A.locked_values();

        // target size
        index_type n_rows = A.height();
        index_type n_cols = base_data_t::_S;

        int nnz = 0;
        int *indptr_new = new int[n_cols + 1];
        std::vector<int> final_rows(A.nonzeros());
        std::vector<value_type> final_vals(A.nonzeros());

        indptr_new[0] = 0;

        // we adapt transversal order for this case
        //XXX: or transpose A (maybe better for cache)
        std::vector< std::vector<int> > inv_mapping(base_data_t::_S);
        for(int idx = 0; idx < base_data_t::row_idx.size(); ++idx) {
            inv_mapping[base_data_t::row_idx[idx]].push_back(idx);
        }

        std::vector<index_type> idx_map(n_rows, -1);

        for(index_type target_col = 0; target_col < base_data_t::_S;
            ++target_col) {

            std::vector<int>::iterator itr;
            for(itr = inv_mapping[target_col].begin();
                itr != inv_mapping[target_col].end(); itr++) {

                int col = *itr;

                for(index_type idx = indptr[col]; idx < indptr[col + 1]; idx++) {

                    index_type row = indices[idx];
                    value_type val = values[idx] * base_data_t::row_value[col];

                    //XXX: I think we should get rid of the if here...
                    if(idx_map[row] == -1) {
                        idx_map[row] = nnz;
                        final_rows[nnz] = row;
                        final_vals[nnz] = val;
                        nnz++;
                    } else {
                        final_vals[idx_map[row]] += val;
                    }
                }
            }

            indptr_new[target_col + 1] = nnz;

            // reset idx_map
            for(int i = indptr_new[target_col]; i < nnz; ++i)
                idx_map[final_rows[i]] = -1;
        }

        int *indices_new = new int[nnz];
        std::copy(final_rows.begin(), final_rows.begin() + nnz, indices_new);

        double *values_new = new double[nnz];
        std::copy(final_vals.begin(), final_vals.begin() + nnz, values_new);

        sketch_of_A.attach(indptr_new, indices_new, values_new,
                           nnz, n_rows, n_cols, true);
    }
};

} } /** namespace skylark::sketch */

#endif // SKYLARK_HASH_TRANSFORM_LOCAL_SPARSE_HPP
