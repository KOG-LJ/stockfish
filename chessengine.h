#pragma once

#include <base/base.h>

namespace ChessNetwork {

// ChessEngine
class ChessEngine {
public:
    static ChessEngine* Create();

    virtual ~ChessEngine() {
    }
    virtual bool Initialize(int hashTableSizeInMegaBytes, int maxMoveCount) = 0;
    virtual bool SetOpeningBook(char* openingBookBinary, int openingBookBinarySize) = 0;
    virtual int GenerateMoves(const String& fenString, int minTime, int maxTime, int elo, bool useOpeningBook) = 0;
    virtual int GenerateMovesWithSkill(const String& fenString, int minTime, int maxTime, int skill, int maxDepth, int contempt, bool useOpeningBook) = 0;
    virtual String GetMove(int index) const = 0;
    virtual float GetMoveScore(int index) const = 0;
    virtual int GetMoveDepth(int index) const = 0;
    virtual int GetMoveCompletedDepth(int index) const = 0;
};

} // namespace ChessNetwork