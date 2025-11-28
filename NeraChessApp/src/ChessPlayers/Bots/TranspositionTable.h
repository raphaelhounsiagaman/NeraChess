#pragma once
#include "ChessBoard.h"

enum class EntryFlag : uint8_t
{
	EXACT,
	LOWERBOUND,
	UPPERBOUND
};

struct TTEntry
{
	uint64_t zobristKey = 0;        // zobrist hash
	float value = 0;       // eval from search
	int8_t depth = -1;        // search depth
	EntryFlag flag = EntryFlag::EXACT;       // exact/lower/upper
	Move bestMove = 0;  // packed move
	int16_t age = 0;         // ply or generation
};

class TranspositionTable
{
public:

	explicit TranspositionTable(size_t megabytes, int clusterSize = 4);

	~TranspositionTable();
	
	void Clear();

	// Look up by key
	TTEntry* Probe(uint64_t zobristKey);
	
	// Store entry
	void Store(
		uint64_t zobristKey,
		float value,
		int8_t depth,
		EntryFlag flag,
		uint32_t bestMove,
		int age);

private:

	

	size_t GetClusterIndex(uint64_t zobristKey) const;

	void Write(TTEntry& e,
		uint64_t zobristKey,
		float value,
		int8_t depth,
		EntryFlag flag,
		uint32_t bestMove,
		int32_t age);

	int ReplacementScore(const TTEntry& e, int newDepth, int newAge) const;

private:
	TTEntry* m_Table = nullptr;
	size_t   m_NumClusters = 0;
	int      m_ClusterSize = 4;



};