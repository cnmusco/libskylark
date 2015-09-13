#include <iostream>

#include <El.hpp>
#include <boost/mpi.hpp>
#include <boost/format.hpp>

// Some tricks to make compilation faster
#define SKYLARK_NO_ANY
#define SKYLARK_WITH_GAUSSIAN_RFT_ANY
#define SKYLARK_WITH_LAPLACIAN_RFT_ANY
#define SKYLARK_WITH_PPT_ANY

#include <skylark.hpp>

// Algorithms constants
#define CLASSIC_KRR        0
#define FASTER_KRR         1
#define APPROXIMATE_KRR    2

// Kernels constants
#define GAUSSIAN_KERNEL   0
#define LAPLACIAN_KERNEL  1
#define POLYNOMIAL_KERNEL 2

std::string cmdline;
int seed = 38734, algorithm = FASTER_KRR, kernel_type = GAUSSIAN_KERNEL;
int s = 2000, partial = -1;
std::string fname, testname, modelname = "model.dat", logfile = "";
double kp1 = 10.0, kp2 = 0.0, kp3 = 1.0, lambda = 0.01, tolerance=1e-3;
bool use_single;

#ifndef SKYLARK_AVOID_BOOST_PO

#include <boost/program_options.hpp>
namespace bpo = boost::program_options;

int parse_program_options(int argc, char* argv[]) {

    bpo::options_description desc("Options");
    desc.add_options()
        ("help,h", "produce a help message")
        ("trainfile",
            bpo::value<std::string>(&fname),
            "Data to train on (libsvm format).")
        ("testfile",
            bpo::value<std::string>(&testname)->default_value(""),
            "Test data (libsvm format).")
        ("model",
            bpo::value<std::string>(&modelname)->default_value("model.dat"),
            "Name of model file.")
        ("logfile",
            bpo::value<std::string>(&logfile)->default_value(""),
            "File to write log (standard output if empty).")
        ("kernel,k",
             bpo::value<int>(&kernel_type)->default_value(GAUSSIAN_KERNEL),
            "Kernel to use (0: Gaussian, 1: Laplacian, 2: Polynomial).")
        ("algorithm,a",
             bpo::value<int>(&algorithm)->default_value(FASTER_KRR),
            "Algorithm to use (0: Classic, 1: Faster (Precond), "
            "2: Approximate (Random Features)). OPTIONAL.")
        ("seed,s",
            bpo::value<int>(&seed)->default_value(38734),
            "Seed for random number generation. OPTIONAL.")
        ("kernelparam,g",
            bpo::value<double>(&kp1),
            "Kernel parameter. REQUIRED.")
        ("kernelparam2,x",
            bpo::value<double>(&kp2)->default_value(0.0),
            "If Applicable - Second Kernel Parameter (Polynomial Kernel: c).")
        ("kernelparam3,y",
            bpo::value<double>(&kp3)->default_value(1.0),
            "If Applicable - Third Kernel Parameter (Polynomial Kernel: gamma).")
        ("lambda,l",
            bpo::value<double>(&lambda)->default_value(0.01),
            "Lambda regularization parameter.")
        ("tolerance,t",
            bpo::value<double>(&tolerance)->default_value(1e-3),
            "Tolerance for the iterative method (when used).")
        ("partial,p",
            bpo::value<int>(&partial)->default_value(-1),
            "Load only specified quantity examples from training. "
            "Will read all if -1.")
        ("single", "Whether to use single precision instead of double.")
        ("numfeatures,f",
            bpo::value<int>(&s),
            "Number of random features.");

    bpo::positional_options_description positional;
    positional.add("trainfile", 1);
    positional.add("testfile", 2);

    bpo::variables_map vm;
    try {
        bpo::store(bpo::command_line_parser(argc, argv)
            .options(desc).positional(positional).run(), vm);

        if (vm.count("help")) {
            std::cout << "Usage: " << argv[0]
                      << " [options] input-file-name [test-file-name]"
                      << std::endl;
            std::cout << desc;
            return 0;
        }

        if (!vm.count("trainfile")) {
            std::cout << "Input file is required." << std::endl;
            return -1;
        }

        bpo::notify(vm);

        use_single = vm.count("single");

    } catch(bpo::error& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << desc << std::endl;
        return -1;
    }

    return 1000;
}


#else

int parse_program_options(int argc, char* argv[]) {

    int poscount = 0;
    for (int i = 1; i < argc; i += 2) {
        std::string flag = argv[i];
        std::string value = i + 1 < argc ? argv[i+1] : "";

        if (flag == "--seed" || flag == "-s")
            seed = boost::lexical_cast<int>(value);

        if (flag == "--lambda" || flag == "-l")
            lambda = boost::lexical_cast<double>(value);

        if (flag == "--tolerance" || flag == "-t")
            tolerance = boost::lexical_cast<double>(value);

        if (flag == "--partial" || flag == "-p")
            partial = boost::lexical_cast<int>(value);

        if (flag == "--kernelparam" || flag == "-g")
            kp1 = boost::lexical_cast<double>(value);

        if (flag == "--kernelparam2" || flag == "-x")
            kp2 = boost::lexical_cast<double>(value);

        if (flag == "--kernelparam3" || flag == "-z")
            kp3 = boost::lexical_cast<double>(value);

        if (flag == "--kernel" || flag == "-k")
            kernel_type = boost::lexical_cast<int>(value);

        if (flag == "--algorithm" || flag == "-a")
            algorithm = boost::lexical_cast<int>(value);

        if (flag == "--nunmfeatures" || flag == "-f")
            s = boost::lexical_cast<int>(value);

        if (flag == "--single") {
            use_single = true;
            i--;
        }

        if (flag == "--trainfile")
            fname = value;

        if (flag == "--logfile")
            logfile = value;

        if (flag == "--model")
            modelname = value;

        if (flag == "--testfile")
            testname = value;

        if (flag[0] != '-' && poscount != 0)
            testname = flag;

        if (flag[0] != '-' && poscount == 0) {
            fname = flag;
            poscount++;
        }

        if (flag[0] != '-')
            i--;
    }

    return 1000;
}

#endif

template<typename T>
int execute(skylark::base::context_t &context) {

    boost::mpi::communicator world;
    int rank = world.rank();

    std::ostream *log_stream = &std::cout;
    if (rank == 0 && logfile != "") {
        log_stream = new std::ofstream();
        ((std::ofstream *)log_stream)->open(logfile);
    }

    El::DistMatrix<T> X;
    El::DistMatrix<El::Int> L;

    boost::mpi::timer timer;

    if (rank == 0) {
        *log_stream << "# Generated using kernel_regression ";
        *log_stream << "using the following command-line: " << std::endl;
        *log_stream << "#\t" << cmdline << std::endl;
        *log_stream << "# Number of ranks is " << world.size() << std::endl;
    }

    // Load X and Y
    if (rank == 0) {
        *log_stream << "Reading the matrix... ";
        log_stream->flush();
        timer.restart();
    }

    skylark::utility::io::ReadLIBSVM(fname, X, L, skylark::base::COLUMNS,
        0, partial);

    if (rank == 0)
        *log_stream <<"took " << boost::format("%.2e") % timer.elapsed()
                  << " sec\n";

    // Training
    if (rank == 0) {
        *log_stream << "Training... " << std::endl;
        timer.restart();
    }

    std::shared_ptr<skylark::ml::kernel_t> k_ptr;

    switch (kernel_type) {
    case GAUSSIAN_KERNEL:
        k_ptr.reset(new skylark::ml::gaussian_t(X.Height(), kp1));
        break;

    case LAPLACIAN_KERNEL:
        k_ptr.reset(new skylark::ml::laplacian_t(X.Height(), kp1));
        break;

    case POLYNOMIAL_KERNEL:
        k_ptr.reset(new skylark::ml::polynomial_t(X.Height(), kp1, kp2, kp3));
        break;

    default:
        *log_stream << "Invalid kernel specified." << std::endl;
        return -1;
    }

    skylark::ml::kernel_container_t k(k_ptr);

    El::DistMatrix<T> A, W;
    std::vector<El::Int> rcoding;

    skylark::sketch::sketch_transform_container_t<El::DistMatrix<T>,
                                                  El::DistMatrix<T> > S;

    skylark::ml::rlsc_params_t rlsc_params(rank == 0, 4, *log_stream, "\t");
    rlsc_params.tolerance = tolerance;

    skylark::ml::model_t<El::Int, T> *model;

    switch(algorithm) {
    case CLASSIC_KRR:
        skylark::ml::KernelRLSC(skylark::base::COLUMNS, k, X, L,
            T(lambda), A, rcoding, rlsc_params);
        model =
            new skylark::ml::kernel_model_t<skylark::ml::kernel_container_t,
                  El::Int, T>(k, skylark::base::COLUMNS, X, fname, A, rcoding);
        break;

    case FASTER_KRR:
        skylark::ml::FasterKernelRLSC(skylark::base::COLUMNS, k, X, L,
            T(lambda), A, rcoding, s, context, rlsc_params);
        model =
            new skylark::ml::kernel_model_t<skylark::ml::kernel_container_t,
                  El::Int, T>(k, skylark::base::COLUMNS, X, fname, A, rcoding);
        break;

    case APPROXIMATE_KRR:
        skylark::ml::ApproximateKernelRLSC(skylark::base::COLUMNS, k, X, L,
            T(lambda), S, W, rcoding, s, context, rlsc_params);
        model =
            new skylark::ml::feature_expansion_model_t<
                skylark::sketch::sketch_transform_container_t, El::Int, T>
            (S, W, rcoding);
        break;

    default:
        *log_stream << "Invalid algorithm value specified." << std::endl;
        return -1;
    }

    if (rank == 0)
        *log_stream << "Training took " << boost::format("%.2e") % timer.elapsed()
                  << " sec\n";

    // Save model
    if (rank == 0) {
        *log_stream << "Saving model... ";
        log_stream->flush();
        timer.restart();
    }

    boost::property_tree::ptree pt = model->to_ptree();

    if (rank == 0) {
        std::ofstream of(modelname);
        of << "# Generated using kernel_regression ";
        of << "using the following command-line: " << std::endl;
        of << "#\t" << cmdline << std::endl;
        of << "# Number of ranks is " << world.size() << std::endl;
        boost::property_tree::write_json(of, pt);
        of.close();
    }

    if (rank == 0)
        *log_stream <<"took " << boost::format("%.2e") % timer.elapsed()
                  << " sec\n";

    // Test
    if (!testname.empty()) {
        if (rank == 0) {
            *log_stream << "Predicting... ";
            log_stream->flush();
            timer.restart();
        }

        El::DistMatrix<T> XT;
        El::DistMatrix<El::Int> LT;
        skylark::utility::io::ReadLIBSVM(testname, XT, LT,
            skylark::base::COLUMNS, X.Height());

        El::DistMatrix<El::Int> LP;
        model->predict(skylark::base::COLUMNS, XT, LP);

        if (rank == 0)
            *log_stream << "took " << boost::format("%.2e") % timer.elapsed()
                      << " sec\n";

        int errs = 0;
        if (LT.LocalHeight() > 0)
            for(int i = 0; i < LT.LocalWidth(); i++)
                if (LT.GetLocal(0, i) != LP.GetLocal(0, i))
                    errs++;

        errs = El::mpi::AllReduce(errs, MPI_SUM, LT.DistComm());

        if (rank == 0)
            *log_stream << "Error rate: "
                      << boost::format("%.2f") % ((errs * 100.0) / LT.Width())
                      << "%" << std::endl;
    }

    if (rank == 0 && logfile != "") {
        ((std::ofstream *)log_stream)->close();
        delete log_stream;
    }

    delete model;

    return 0;
}

int main(int argc, char* argv[]) {

    for(int i = 0; i < argc; i++) {
        cmdline.append(argv[i]);
        if (i < argc - 1)
            cmdline.append(" ");
    }

    El::Initialize(argc, argv);

    boost::mpi::communicator world;
    int rank = world.rank();

    int flag = parse_program_options(argc, argv);

    if (flag != 1000)
        return flag;

    skylark::base::context_t context(seed);

    int ret = -1;

    SKYLARK_BEGIN_TRY()

        if (use_single)
            ret = execute<float>(context);
        else
            ret = execute<double>(context);

    SKYLARK_END_TRY() SKYLARK_CATCH_AND_PRINT((rank == 0))

        catch (const std::exception& ex) {
            if (rank == 0) SKYLARK_PRINT_EXCEPTION_DETAILS(ex);
        }

    El::Finalize();

    return ret;
}