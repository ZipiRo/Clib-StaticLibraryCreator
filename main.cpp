#include <iostream>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <string>
#include <cstdlib>

namespace fs = std::filesystem;

// ANSI color codes
const std::string BLUE    = "\033[34m";
const std::string GREEN   = "\033[32m";
const std::string RED     = "\033[31m";
const std::string MAGENTA = "\033[35m";
const std::string YELLOW  = "\033[33m";
const std::string RESET   = "\033[0m";
const std::string ITALIC  = "\033[3m";

// File to store last compilation info
const std::string SIZE_FILE = ".last_sizes.txt";

struct FileInfo {
    uintmax_t cppSize = 0;
    uintmax_t objTime = 0;
};

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <source_dir> <include_dir> <output_dir> <static_lib_name>\n";
        return 1;
    }

    fs::path sourceDir  = argv[1];
    fs::path includeDir = argv[2];
    fs::path outputDir  = argv[3];
    std::string staticLib = argv[4];

    // Load previous info
    std::unordered_map<std::string, FileInfo> lastInfo;
    std::ifstream inFile(SIZE_FILE);
    if (inFile.is_open()) {
        std::string file;
        uintmax_t cppSize, objTime;
        while (inFile >> file >> cppSize >> objTime) {
            lastInfo[file] = {cppSize, objTime};
        }
    }
    inFile.close();

    // Ensure output directory exists
    if (!fs::exists(outputDir)) fs::create_directories(outputDir);

    std::unordered_map<std::string, FileInfo> newInfo;
    bool anyCompiled = false;

    // Compile changed or new .cpp files
    for (const auto& entry : fs::directory_iterator(sourceDir)) {
        if (entry.path().extension() == ".cpp" && entry.path().filename() != "main.cpp") {
            std::string filepath = entry.path().string();
            uintmax_t filesize = fs::file_size(entry.path());
            fs::path objFile = outputDir / (entry.path().stem().string() + ".o");

            newInfo[filepath].cppSize = filesize;
            newInfo[filepath].objTime = fs::exists(objFile) ? fs::last_write_time(objFile).time_since_epoch().count() : 0;

            bool needsCompile = false;

            auto it = lastInfo.find(filepath);
            if (it == lastInfo.end() || it->second.cppSize != filesize || !fs::exists(objFile)) {
                needsCompile = true;
            }

            if (needsCompile) {
                std::cout << "Compiling " << ITALIC << BLUE << entry.path().filename().string() << RESET << "..." << std::flush;
                std::string cmd = "g++ -c \"" + filepath + "\" -o \"" + objFile.string() + "\" -I\"" + includeDir.string() + "\"";

                if (std::system(cmd.c_str()) != 0) {
                    std::cout << RED << " error" << RESET << "\n";
                    std::cerr << RED << "Compilation of " << entry.path().filename().string() << " failed." << RESET << "\n";
                    return 1;
                }
                std::cout << GREEN << " compiled" << RESET << "\n";
                anyCompiled = true;
                newInfo[filepath].objTime = fs::last_write_time(objFile).time_since_epoch().count();
            }
        }
    }

    if (!anyCompiled) {
        std::cout << GREEN << "No changes detected, skipping compilation.\n" << RESET;
    }

    // Create/Update static library
    bool libUpdated = false;
    fs::path libPath = outputDir / staticLib;

    for (const auto& entry : fs::directory_iterator(outputDir)) {
        if (entry.path().extension() == ".o") {
            std::string objFile = entry.path().string();

            // Only add to library if object is new or updated
            bool addToLib = false;
            for (const auto& [cppFile, info] : newInfo) {
                if ((outputDir / (fs::path(cppFile).stem().string() + ".o")) == entry.path()) {
                    auto it = lastInfo.find(cppFile);
                    if (it == lastInfo.end() || it->second.objTime != info.objTime) {
                        addToLib = true;
                        break;
                    }
                }
            }

            if (addToLib) {
                std::cout << "Adding " << ITALIC << MAGENTA << entry.path().filename().string() << RESET << " to library..." << std::flush;
                std::string cmd = "ar r \"" + libPath.string() + "\" \"" + objFile + "\"";
                if (std::system(cmd.c_str()) != 0) {
                    std::cout << RED << " error" << RESET << "\n";
                    std::cerr << RED << "Failed to add " << entry.path().filename().string() << " to library." << RESET << "\n";
                    return 1;
                }
                fs::remove(entry.path());
                
                std::cout << GREEN << " added" << RESET << "\n";
                libUpdated = true;
            }
        }
    }

    if (!fs::exists(libPath)) {
        std::cout << "Creating static library " << YELLOW << staticLib << RESET << "...\n";
        std::string cmd = "ar r \"" + libPath.string() + "\" \"" + outputDir.string() + "/*.o\"";
        if (std::system(cmd.c_str()) != 0) {
            std::cerr << RED << "Failed to create library " << staticLib << RESET << "\n";
            return 1;
        }
        libUpdated = true;
    }

    if (!libUpdated) {
        std::cout << GREEN << "Static library " << staticLib << " is up to date.\n" << RESET;
    } else {
        std::cout << YELLOW << "Static library " << staticLib << " updated successfully.\n" << RESET;
    }

    // Save new compilation info
    std::ofstream outFile(SIZE_FILE);
    for (const auto& [file, info] : newInfo) {
        outFile << file << " " << info.cppSize << " " << info.objTime << "\n";
    }

    return 0;
}