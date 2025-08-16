#pragma once

#include <cstdint>
#include <array>

struct UndoInfo 
{
    uint16_t move;          // the move made (our 32-bit packed Move)
    uint8_t capturedPiece;  // piece that was captured, 0 if none
    uint8_t castlingRights; // old castling rights
    int8_t  enPassantFile;// old en passant square, -1 if none
    uint16_t halfmoveClock; // for 50-move rule
};

struct UndoStack 
{
    std::array<UndoInfo, 512> data;
    int16_t count = 0;

    inline void push(const UndoInfo& info) noexcept {
        data[count++] = info;
    }

    inline UndoInfo pop() noexcept {
        return data[--count];
    }

    inline UndoInfo& top() noexcept { return data[count - 1]; }
};