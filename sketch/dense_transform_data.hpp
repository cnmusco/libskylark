#ifndef SKYLARK_DENSE_TRANSFORM_DATA_HPP
#define SKYLARK_DENSE_TRANSFORM_DATA_HPP

#include <vector>

#include "../base/context.hpp"
#include "../utility/randgen.hpp"

#include "transform_data.hpp"

namespace skylark { namespace sketch {

/**
 * This is the base data class for dense transforms. Essentially, it
 * holds the input and sketched matrix sizes and the array of samples
 * to be lazily computed.
 */
template <typename ValueType,
          template <typename> class ValueDistribution>
struct dense_transform_data_t : public transform_data_t {
    // For reasons of naming consistency
    typedef ValueType value_type;
    typedef ValueDistribution<ValueType> value_distribution_type;

    /**
     * Regular constructor
     */
    dense_transform_data_t (int N, int S, skylark::base::context_t* context,
                            std::string type = "")
        : transform_data_t(N, S, context, type),
          distribution(),
          random_samples(
                  context->allocate_random_samples_array(N * S, distribution)) {

        // No scaling in "raw" form
        scale = 1.0;
    }

    dense_transform_data_t (const boost::property_tree::ptree json)
        : transform_data_t(json),
          distribution(),
          random_samples(
            _creation_context->allocate_random_samples_array(
                _N * _S, distribution)) {

        // No scaling in "raw" form
        scale = 1.0;
    }

protected:
    value_distribution_type distribution; /**< Distribution for samples */
    skylark::utility::random_samples_array_t < value_distribution_type>
        random_samples;
    /**< Array of samples, to be lazily computed */
    double scale; /**< Scaling factor for the samples */
};

} } /** namespace skylark::sketch */

#endif /** SKYLARK_DENSE_TRANSFORM_DATA_HPP */
