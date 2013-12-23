/**
 *  This test ensures that the sketch application (for Elemental matrices) is
 *  done correctly (on-the-fly matrix multiplication in the code is compared
 *  to true matrix multiplication).
 *  This test builds on the following assumptions:
 *
 *      - elem::Gemm returns the correct result, and
 *      - the random numbers in row_idx and row_value (see
 *        hash_transform_data_t) are drawn from the promised distributions.
 */


#include <vector>

#include <elemental.hpp>

#include "../../utility/distributions.hpp"
#include "../../sketch/hash_transform.hpp"

#include <boost/mpi.hpp>
#include <boost/test/minimal.hpp>

typedef boost::random::uniform_int_distribution<int> uniform_t;

template < typename InputMatrixType,
           typename OutputMatrixType = InputMatrixType >
struct Dummy_t : public skylark::sketch::hash_transform_t<
    InputMatrixType, OutputMatrixType,
    uniform_t, skylark::utility::rademacher_distribution_t > {

    typedef skylark::sketch::hash_transform_t<
        InputMatrixType, OutputMatrixType,
        uniform_t, skylark::utility::rademacher_distribution_t >
            hash_t;

    Dummy_t(int N, int S, skylark::sketch::context_t& context)
        : skylark::sketch::hash_transform_t<InputMatrixType, OutputMatrixType,
          uniform_t, skylark::utility::rademacher_distribution_t>(N, S, context)
    {}

    std::vector<int> getRowIdx() { return hash_t::row_idx; }
    std::vector<double> getRowValues() { return hash_t::row_value; }
};

int test_main(int argc, char *argv[]) {

    //////////////////////////////////////////////////////////////////////////
    //[> Parameters <]

    //FIXME: use random sizes?
    const size_t n   = 10;
    const size_t m   = 5;
    const size_t n_s = 6;
    const size_t m_s = 3;

    //////////////////////////////////////////////////////////////////////////
    //[> Setup test <]
    namespace mpi = boost::mpi;

    typedef elem::Matrix<double> MatrixType;
    typedef elem::DistMatrix<double, elem::VR, elem::STAR> DistMatrixType;

    mpi::environment env(argc, argv);
    mpi::communicator world;

    elem::Initialize(argc, argv);
    MPI_Comm mpi_world(world);
    elem::Grid grid(mpi_world);

    skylark::sketch::context_t context (0, world);

    double count = 1.0;
    elem::DistMatrix<double, elem::VR, elem::STAR> A(grid);
    elem::Uniform (A, n, m);
    for( size_t j = 0; j < A.Height(); j++ )
        for( size_t i = 0; i < A.Width(); i++ )
            A.Set(j, i, count++);


    //////////////////////////////////////////////////////////////////////////
    //[> Column wise application <]

    /* 1. Create the sketching matrix */
    Dummy_t<DistMatrixType, MatrixType> Sparse(n, n_s, context);
    std::vector<int> row_idx    = Sparse.getRowIdx();
    std::vector<double> row_val = Sparse.getRowValues();

    // PI generated by random number gen
    elem::DistMatrix<double, elem::VR, elem::STAR> pi_sketch(grid);
    elem::Uniform(pi_sketch, n_s, n);
    elem::Zero(pi_sketch);
    for(size_t i = 0; i < row_idx.size(); ++i)
        pi_sketch.Set(row_idx[i], i, row_val[i]);

    /* 2. Create space for the sketched matrix */
    MatrixType sketch_A(n_s, m);

    /* 3. Apply the transform */
    Sparse.apply(A, sketch_A, skylark::sketch::columnwise_tag());

    /* 4. Build structure to compare */
    elem::DistMatrix<double, elem::VR, elem::STAR> expected_A(grid);
    elem::Uniform (expected_A, n_s, m);
    elem::Gemm(elem::NORMAL, elem::NORMAL,
               1.0, pi_sketch.LockedMatrix(), A.LockedMatrix(),
               0.0, expected_A.Matrix());

    for(size_t j = 0; j < sketch_A.Height(); j++ )
        for(size_t i = 0; i < sketch_A.Width(); i++ )
            if(sketch_A.Get(j, i) != expected_A.Get(j,i)) {
                std::cerr << sketch_A.Get(j, i)  << " != "
                          << expected_A.Get(j, i) << std::endl;
                BOOST_FAIL("Result of colwise application not as expected");
            }



    //////////////////////////////////////////////////////////////////////////
    //[> Row wise application <]

    //[> 1. Create the sketching matrix <]
    Dummy_t<DistMatrixType, MatrixType> Sparse_r (m, m_s, context);
    row_idx.clear(); row_val.clear();
    row_idx = Sparse_r.getRowIdx();
    row_val = Sparse_r.getRowValues();

    // PI^T generated by random number gen
    elem::DistMatrix<double, elem::VR, elem::STAR> pi_sketch_r(grid);
    elem::Uniform(pi_sketch_r, m, m_s);
    elem::Zero(pi_sketch_r);
    for(size_t i = 0; i < row_idx.size(); ++i)
        pi_sketch_r.Set(i, row_idx[i], row_val[i]);

    //[> 2. Create space for the sketched matrix <]
    MatrixType sketch_A_r(n, m_s);

    //[> 3. Apply the transform <]
    Sparse_r.apply (A, sketch_A_r, skylark::sketch::rowwise_tag());

    /* 4. Build structure to compare */
    elem::DistMatrix<double, elem::VR, elem::STAR> expected_AR(grid);
    elem::Uniform (expected_AR, n, m_s);
    elem::Gemm(elem::NORMAL, elem::NORMAL,
               1.0, A.LockedMatrix(), pi_sketch_r.Matrix(),
               0.0, expected_AR.Matrix());

    for(size_t j = 0; j < sketch_A_r.Height(); j++ )
        for(size_t i = 0; i < sketch_A_r.Width(); i++ )
            if(sketch_A_r.Get(j, i) != expected_AR.Get(j,i)) {
                std::cerr << sketch_A_r.Get(j, i)  << " != "
                          << expected_AR.Get(j, i) << std::endl;
                BOOST_FAIL("Result of rowwise application not as expected");
            }


    elem::Finalize();
    return 0;
}
