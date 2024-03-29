#include <adios2.h>

#include <chrono>
#include <iostream>
#include <thread>

#include <string.h>

#define DATA_TRANSPORT "WAN"

using namespace std;
using namespace std::chrono;

int adios_writer(std::string path, unsigned long msz_size, unsigned long msz_count)
{
    adios2::ADIOS ad;
    adios2::IO io;
    adios2::Engine writer;
    adios2::Variable<char> data;
    std::vector<char> test_data;
    high_resolution_clock::time_point t1, t2;
    double duration, total_size;
    unsigned long i;

    // initialize adios
    ad = adios2::ADIOS(MPI_COMM_SELF, true);
    io = ad.DeclareIO("writer");
    io.SetEngine("SST");
    io.SetParameters({{"RendezvousReaderCount", "1"}, {"DataTransport", DATA_TRANSPORT}}); // don't wait on readers to connect
    //io.SetParameters({{"RendezvousReaderCount", "1"}});

    data = io.DefineVariable<char>("data", {msz_size}, {0}, {msz_size}, false);
    test_data.resize(msz_size*msz_count);
    for (i = 0; i < msz_size*msz_count; i++) {
        unsigned long num = i%msz_size;
        test_data[i] = char(num%255);
    }
    //std::fill(test_data.begin(), test_data.end(), 0);

    writer = io.Open(path, adios2::Mode::Write);

    std::cout << "[ADIOS] Start writing: " << msz_size << ", " << msz_count << std::endl;
    
    // Promise that no more definitions or changes to defined variables will occur.
    // Useful information if called before the first EndStep() of an output Engine. 
    //writer.LockWriterDefinitions();

    t1 = high_resolution_clock::now();
    for (i = 0; i < msz_count; i++)
    {
        writer.BeginStep(adios2::StepMode::Update);
        writer.Put(data, test_data.data());
        writer.EndStep();
        //std::cout << "Wrote " << i << "-th data!" << std::endl;
    }
    t2 = high_resolution_clock::now();
    std::cout << "[ADIOS] End writing: " << i << std::endl;
    writer.Close();

    {
        total_size = double(msz_count) * double(msz_size) / 1024.0 / 1024.0; // MBytes
        duration = (double)duration_cast<milliseconds>(t2 - t1).count() / 1000.0; // sec
        std::cout << "[ADIOS WRITER]\n" 
                  << "Total # messages : " << msz_count << "\n"
                  << "Message size     : " << msz_size << " Bytes\n"
                  << "Total size       : " << total_size << " MBytes\n"
                  << "Total time       : " << duration << " seconds\n"
                  << "Throughput       : " << total_size / duration << " MBytes/sec\n"
                  << std::endl; 
    }

    return 0;
}

int adios_reader(std::string path, unsigned long msz_size, unsigned long msz_count, bool bCheck)
{
    adios2::ADIOS ad;
    adios2::IO io;
    adios2::Engine reader;
    adios2::StepStatus status;
    unsigned long step;
    adios2::Variable<char> data;
    std::vector<char> test_data;
    high_resolution_clock::time_point t1, t2;
    double duration, total_size;

    // initialize adios
    ad = adios2::ADIOS(MPI_COMM_SELF, true);
    io = ad.DeclareIO("reader");
    io.SetEngine("SST");
    io.SetParameters({{"DataTransport", DATA_TRANSPORT}});
    reader = io.Open(path, adios2::Mode::Read, MPI_COMM_SELF);

    step = 0;
    test_data.resize(msz_size);

    std::cout << "[ADIOS] Start reading ..." << std::endl;
    //t1 = high_resolution_clock::now();
    while (step < msz_count)
    {
        int n_tries = 0;
        do {
            status = reader.BeginStep(adios2::StepMode::Read);
            n_tries++;
            if (status == adios2::StepStatus::NotReady)
                this_thread::sleep_for(microseconds(100));
        } while (status == adios2::StepStatus::NotReady && n_tries < 10000);

        if (status != adios2::StepStatus::OK){
            std::cout << "Terminate at " << step << "(" << n_tries << ")" << std::endl;
            break;
        }
        //std::cout << "Step: " << step << ", trie: " << n_tries << std::endl;

        if (step == 0) {
            //reader.LockReaderSelections();
            t1 = high_resolution_clock::now();
        }

        data = io.InquireVariable<char>("data");
        if (data)
        {
            data.SetSelection({{0}, {msz_size}});
            //reader.Get<char>(data, test_data.data(), adios2::Mode::Sync);
            reader.Get<char>(data, test_data.data());
            //std::cout << "Get " << step << "-th data!" << std::endl;
        }
        else
        {
            std::cout << "Failed to inquire variable!" << std::endl;
        }
        reader.EndStep();

        if (bCheck && data)
        {
            for (size_t i = 0; i < test_data.size(); i++)
                if (test_data[i] != char(i%255))
                {
                    std::cout << "Incorrect data: " << test_data[i] << " vs. " << char(i%255) << std::endl;
                    break;
                }
        }

        step++;
    }
    t2 = high_resolution_clock::now();
    std::cout << "[ADIOS] End reading: " << step << std::endl;
    reader.Close();

    {
        total_size = double(step) * double(msz_size) / 1024.0 / 1024.0; // MBytes
        duration = (double)duration_cast<milliseconds>(t2 - t1).count() / 1000.0; // sec
        std::cout << "[ADIOS READER]\n" 
                  << "Total # messages : " << step << "\n"
                  << "Message size     : " << msz_size << " Bytes\n"
                  << "Total size       : " << total_size << " MBytes\n"
                  << "Total time       : " << duration << " seconds\n"
                  << "Throughput       : " << total_size / duration << " MBytes/sec\n"
                  << std::endl;         
    }

    return 0;
}


int main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);

    int wrank, wsize;
    MPI_Comm_rank(MPI_COMM_WORLD, &wrank);
    MPI_Comm_size(MPI_COMM_WORLD, &wsize);

    int role = atoi(argv[1]); 
    unsigned long msz_size = (unsigned long)atoi(argv[2]);
    unsigned long msz_count = (unsigned long)atoi(argv[3]);
    int check = atoi(argv[4]);
 
    if (role == 0)
        adios_reader("test.bp", msz_size, msz_count, check==1);
    else
        adios_writer("test.bp", msz_size, msz_count);

    MPI_Finalize();
    return 0;
}
