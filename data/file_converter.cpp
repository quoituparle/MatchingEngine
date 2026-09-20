#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>

enum struct Side : uint8_t { Buy, Sell };

#pragma pack(push, 1)
struct BinaryOrderData {
    uint64_t id;
    uint64_t price;
    uint64_t qty;
    uint64_t quote_qty;
    uint64_t time;
    Side side;
    bool best_match;
}; // total size 42 bytes.
#pragma pack(pop)

static_assert(sizeof(BinaryOrderData) == 42, "Data structure must be 42 bytes.");

// Binance file structure: ID, price, qty, quote quantity(cash of USDT), timestamp, is_buyer-maker status(T maker/F taker), best-match status(T/F).

Side ParseSide (const std::string& s) {
    return (s == "TRUE") ? Side::Sell : Side::Buy;
};

bool ParseBestMatch (const std::string& s) {
    return (s == "TRUE") ? true : false;
}

void Convert(const std::string& csvPath, const std::string& binPath) {
    std::ifstream csvFile(csvPath);
    if (!csvFile.is_open()) {
        std::cerr << "Failed to open CSV file: " << csvPath << std::endl;
        return;
    }

    std::ofstream binFile(binPath, std::ios::binary);
    if (!binFile.is_open()) {
        std::cerr << "Failed to create BIN file: " << binPath << std::endl;
        return;
    }

    uint64_t recordCount = 0;
    BinaryOrderData data {};

    std::cout << "starting converting" << std::endl;

    std::string line;

    while (std::getline(csvFile, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string field;

        std::getline(ss, field, ','); data.id = std::stoull(field);
        std::getline(ss, field, ','); data.price = static_cast<uint64_t>(std::stod(field) * 1e8);
        std::getline(ss, field, ','); data.qty = static_cast<uint64_t>(std::stod(field) * 1e8); // avoid float bias
        std::getline(ss, field, ','); data.quote_qty = static_cast<uint64_t>(std::stod(field) * 1e8);
        std::getline(ss, field, ','); data.time = std::stoull(field);
        std::getline(ss, field, ','); data.side = ParseSide(field);
        std::getline(ss, field, ','); data.best_match = ParseBestMatch(field);

        binFile.write(reinterpret_cast<const char*>(&data), sizeof(BinaryOrderData));
        recordCount++;
    }
    std::cout << "Record count now " << recordCount << std::endl;
}

int main() {
    Convert("BTCUSDT-trades-2026-09-18.csv", "output.bin");
    return 0;
}