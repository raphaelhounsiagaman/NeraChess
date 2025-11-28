#include "TranspositionTable.h"

TranspositionTable::TranspositionTable(size_t megabytes, int clusterSize)
	: m_NumClusters(clusterSize)
{
	size_t bytes = megabytes * 1024ULL * 1024ULL;
	size_t entrySize = sizeof(TTEntry) * clusterSize;

	m_NumClusters = bytes / entrySize;
	if (m_NumClusters == 0) m_NumClusters = 1;

	m_Table = new TTEntry[m_NumClusters * clusterSize];
	Clear();
}

TranspositionTable::~TranspositionTable()
{
	delete[] m_Table;
}

void TranspositionTable::Clear()
{
	for (size_t i = 0; i < m_NumClusters * m_ClusterSize; i++)
		m_Table[i] = TTEntry{};
}

TTEntry* TranspositionTable::Probe(uint64_t zobristKey)
{
	size_t idx = GetClusterIndex(zobristKey);
	TTEntry* cluster = &m_Table[idx * m_ClusterSize];
	for (int i = 0; i < m_ClusterSize; i++) {
		if (cluster[i].zobristKey == zobristKey)
			return &cluster[i];
	}
	return nullptr;
}

void TranspositionTable::Store(
	uint64_t zobristKey,
	float value, 
	int8_t depth, 
	EntryFlag flag, 
	uint32_t bestMove,
	int age)
{
	size_t idx = GetClusterIndex(zobristKey);
	TTEntry* cluster = &m_Table[idx * m_ClusterSize];

	// Step 1: try replacing an exact-key entry
	for (int i = 0; i < m_ClusterSize; ++i) {
		if (cluster[i].zobristKey == zobristKey) {
			Write(cluster[i], zobristKey, value, depth, flag, bestMove, age);
			return;
		}
	}

	// Step 2: choose replacement victim
	int victim = 0;
	int bestScore = -1000000000;

	for (int i = 0; i < m_ClusterSize; ++i) {
		int score = ReplacementScore(cluster[i], depth, age);
		if (score > bestScore) {
			bestScore = score;
			victim = i;
		}
	}

	Write(cluster[victim], zobristKey, value, depth, flag, bestMove, age);
}

inline size_t TranspositionTable::GetClusterIndex(uint64_t zobristKey) const
{
	return zobristKey % m_NumClusters;
}

void TranspositionTable::Write(TTEntry& e, uint64_t key, float value, int8_t depth, EntryFlag flag, uint32_t bestMove, int32_t age)
{
	e.zobristKey = key;
	e.value = value;
	e.depth = depth;
	e.flag = flag;
	e.bestMove = bestMove;
	e.age = age;
}

int TranspositionTable::ReplacementScore(const TTEntry& e, int newDepth, int newAge) const
{
	// Common approach:
	// Prefer overwriting:
	//   - shallow entries
	//   - old entries
	//   - empty entries (key == 0)
	if (e.zobristKey == 0) return 1'000'000'000;

	// Example: weight age more than depth
	int agePenalty = (newAge - e.age);
	int depthPenalty = (e.depth - newDepth);

	return agePenalty * 1024 + depthPenalty;
}
