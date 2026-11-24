#pragma once

// The score is a cosine similarity. Only a completed comparison can allow entry.
constexpr bool allowsOpen(bool compared, double score, double threshold)
{
    return compared && threshold > 0.0 && threshold <= 1.0 &&
           score >= threshold && score <= 1.0;
}
