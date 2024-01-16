#ifndef FILEPATH_H
#define FILEPATH_H

#include <iostream>
#include <string>
#include <random>
#include <sstream>

class FilePath {
public:
    // Constructors
    FilePath() : drive(""), folder(""), fileName(""), uid(generateUid()) {}
    FilePath(const std::string& drive, const std::string& folder, const std::string& fileName)
        : drive(drive), folder(folder), fileName(fileName), uid(generateUid()) {}

    // Member functions to set values
    void setDrive(const std::string& drive) {
        this->drive = drive;
    }

    void setFolder(const std::string& folder) {
        this->folder = folder;
    }

    void setFileName(const std::string& fileName) {
        this->fileName = fileName;
    }

    void setUid(const std::string& newUid) {
        uid = newUid;
    }

    // Member functions to get values
    std::string getDrive() const {
        return drive;
    }

    std::string getFolder() const {
        return folder;
    }

    std::string getFileName() const {
        return fileName;
    }

    std::string getUid() const {
        return uid;
    }

    // Function to display the file path
    void display() const {
        std::cout << "UID: " << uid << "\nDrive: " << drive << "\nFolder: " << folder
                  << "\nFileName: " << fileName << "\n";
    }

private:
    std::string drive;
    std::string folder;
    std::string fileName;
    std::string uid;

    // Function to generate a unique identifier
    static std::string generateUid() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<int> dis(1, 1000);
        
        std::stringstream ss;
        ss << dis(gen);
        return ss.str();
    }
};

#endif // FILEPATH_H