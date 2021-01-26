#include "chessengine.h"
#include <base/memory.h>
// stockfish
#include <stockfish/src/bitboard.h>
#include <stockfish/src/position.h>
#include <stockfish/src/search.h>
#include <stockfish/src/thread.h>
#include <stockfish/src/tt.h>
#include <stockfish/src/uci.h>
#include <stockfish/src/evaluate.h>
#include <stockfish/src/polybook.h>
#include <stockfish/src/syzygy/tbprobe.h>
// std
#include <iostream>
#include <fstream>

//#define TEST

namespace PSQT {
void init();
}

namespace ChessNetwork {

struct MoveInfo {
    Move move;
    int depth = 0;
    int selDepth = 0;
    float score = 0.0f;

    bool operator==(const MoveInfo& other) const {
        return move == other.move;
    }
};

// Globals
static MultiMap<float, MoveInfo> g_bestMoves;
static std::mutex g_thinkLock;

// StockfishChessEngine
class StockfishChessEngine : public ChessEngine {

public:
    virtual ~StockfishChessEngine() {
    }

    void OnMove(Move move, int depth, int selDepth, Value v) {
        MoveInfo moveInfo = {move, depth, selDepth, (float)v};
        if (g_bestMoves.size() > 0) {
            auto pair = g_bestMoves.end();
            pair--;

            int previousDepth = pair->second.depth;
            if (moveInfo.depth > previousDepth) {
                g_bestMoves.clear();
            }
        }

        g_bestMoves.insert({moveInfo.score, moveInfo});
    }

    bool Initialize(int hashTableSizeInMegaBytes, int maxMoveCount) final {
        std::lock_guard<std::mutex> guard(g_thinkLock);
        // Initialize stockfish
        UCI::init(Options);
        Options["Threads"] = std::to_string(1);
        PSQT::init();
        Bitboards::init();
        Position::init();
        Bitbases::init();
        Endgames::init();
        Threads.set((size_t)Options["Threads"]);
        Search::clear();                       // After threads are up
        std::cout.setstate(std::ios::failbit); // Uncomment for console output

        Options["MultiPV"] = std::to_string(maxMoveCount);
        Options["Hash"] = std::to_string(hashTableSizeInMegaBytes);
        Threads.main()->pvCallback = [this](Move move, int depth, int selDepth, Value v) { OnMove(move, depth, selDepth, v); };

        states = StateListPtr(new std::deque<StateInfo>(1));
        return true;
    }

    bool SetOpeningBook(char* openingBookBinary, int openingBookBinarySize) {
        polybook.init(openingBookBinary, openingBookBinarySize);
        return true;
    }

    inline std::string BoolToString(bool b) {
        return b ? "true" : "false";
    }

    int GenerateMoves(const String& fenString, int minTime, int maxTime, int elo, bool useOpeningBook) final {
        std::lock_guard<std::mutex> guard(g_thinkLock);
        Options["Minimum Thinking Time"] = std::to_string(minTime);
        Options["UCI_LimitStrength"] = BoolToString(true);
        Options["UCI_Elo"] = std::to_string(elo);
        Options["Skill Level"] = std::to_string(20); // disabled
        Options["Contempt"] = std::to_string(24);    // default
        Options["OwnBook"] = BoolToString(useOpeningBook);
        position.set(fenString.c_str(), false, &states->back(), Threads.main());
        // Get next move
        Search::LimitsType limits;
        limits.startTime = now();
        limits.movetime = maxTime;

        g_bestMoves.clear();
        StateListPtr new_states(new std::deque<StateInfo>(0));
        new_states->push_back(states->back());
        limits.startTime = now();
        Threads.start_thinking(position, new_states, limits, false);
        Threads.main()->wait_for_search_finished();

        bestMoves.clear();

        int returnValue = -1;
        if (g_bestMoves.size()) {
            for (auto& pair : g_bestMoves) {
                if (std::find(bestMoves.begin(), bestMoves.end(), pair.second) != bestMoves.end()) {
                    continue;
                }
                bestMoves.push_back(pair.second);
            }
            returnValue = (int)bestMoves.size();
        } else if (useOpeningBook && Threads.main()->rootMoves.size()) {
            bestMoves.push_back({Threads.main()->rootMoves[0].pv[0], 0, 0, 0.0f});
        }

        std::reverse(bestMoves.begin(), bestMoves.end());

        return returnValue;
    }

    int GenerateMovesWithSkill(const String& fenString, int minTime, int maxTime, int skill, int maxDepth, int contempt, bool useOpeningBook) final {
        std::lock_guard<std::mutex> guard(g_thinkLock);
        Options["Minimum Thinking Time"] = std::to_string(minTime);
        Options["UCI_LimitStrength"] = BoolToString(false);
        Options["UCI_Elo"] = std::to_string(1350);
        Options["Skill Level"] = std::to_string(skill);
        Options["Contempt"] = std::to_string(contempt);
        Options["OwnBook"] = BoolToString(useOpeningBook);
        position.set(fenString.c_str(), false, &states->back(), Threads.main());
        // Get next move
        Search::LimitsType limits;
        limits.startTime = now();
        limits.depth = maxDepth;
        limits.movetime = maxTime;

        g_bestMoves.clear();
        StateListPtr new_states(new std::deque<StateInfo>(0));
        new_states->push_back(states->back());
        limits.startTime = now();
        Threads.start_thinking(position, new_states, limits, false);
        Threads.main()->wait_for_search_finished();

        bestMoves.clear();

        int returnValue = -1;
        if (g_bestMoves.size()) {
            for (auto& pair : g_bestMoves) {
                if (std::find(bestMoves.begin(), bestMoves.end(), pair.second) != bestMoves.end()) {
                    continue;
                }
                bestMoves.push_back(pair.second);
            }
            returnValue = (int)bestMoves.size();
        } else if (useOpeningBook && Threads.main()->rootMoves.size()) {
            bestMoves.push_back({Threads.main()->rootMoves[0].pv[0], 0, 0, 0.0f});
        }

        std::reverse(bestMoves.begin(), bestMoves.end());

        return returnValue;
    }

    String GetMove(int index) const final {
        if (index >= bestMoves.size()) {
            return {};
        }
        auto moveString = UCI::move(bestMoves[index].move, position.is_chess960());
        return String(moveString.c_str(), moveString.length());
    }

    float GetMoveScore(int index) const final {
        if (index >= bestMoves.size()) {
            return 0.0f;
        }
        return bestMoves[index].score;
    }

    int GetMoveDepth(int index) const final {
        if (index >= bestMoves.size()) {
            return 0;
        }
        return bestMoves[index].depth;
    }

    int GetMoveCompletedDepth(int index) const final {
        if (index >= bestMoves.size()) {
            return 0;
        }
        return bestMoves[index].selDepth;
    }

private:
    StateListPtr states;
    Position position;
    std::vector<MoveInfo> bestMoves;
};

// ChessEngine
ChessEngine* ChessEngine::Create() {
    return new StockfishChessEngine();
}

} // namespace ChessNetwork

#if defined(TEST)
using namespace ChessNetwork;

int main() {
    pBase->Initialize();
    auto ce = ChessEngine::Create();
    ce->Initialize(16, 6);
    bool useOpeningBook = true;
    if (useOpeningBook) {
        std::ifstream file("../../../examples/unity/Assets/OpeningBooks/basic.bin.bytes", std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<char> buffer(size);
            file.read(buffer.data(), size);
            ce->SetOpeningBook(&buffer[0], buffer.size());
        }
    }
    const String START_POS_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    for (int j = 0; j < 4; ++j) {
        printf("Generating move %s\n", j >= 2 ? "Skill" : "ELO");
        auto count = j >= 2 ? ce->GenerateMovesWithSkill(START_POS_FEN, 1000, 1000, 5, 10, 50, j % 2 > 0) : ce->GenerateMoves(START_POS_FEN, 1000, 1000, 1200, j % 2 > 0);
        for (int i = 0; i < std::abs(count); ++i) {
            auto m = ce->GetMove(i);
            if (!m.length()) {
                break;
            }
            printf("%s Move: %d %d %f %s\n", j % 2 > 0 ? "Opening Book" : "Default", ce->GetMoveDepth(i), ce->GetMoveCompletedDepth(i), ce->GetMoveScore(i), m.c_str());
        }
    }
    return 0;
}
#endif