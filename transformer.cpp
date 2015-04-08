#include "macros.h"
#include "transformer.h"

#include "clintscop.h"
#include "clintstmt.h"
#include "clintstmtoccurrence.h"

void ClayTransformer::apply(osl_scop_p scop, const Transformation &transformation) {
  int err = 0;
  clay_beta_normalize(scop);
  switch (transformation.kind()) {
  case Transformation::Kind::Fuse:
    err = clay_fuse(scop, ClayBeta(transformation.target()), m_options);
    break;
  case Transformation::Kind::Split:
    err = clay_split(scop, ClayBeta(transformation.target()), transformation.depth(), m_options);
    break;
  case Transformation::Kind::Shift:
  {
    // FIXME: only constant shifting is supported
    clay_list_p list = clay_list_malloc();
    clay_array_p array = clay_array_malloc();
    clay_array_add(array, transformation.constantAmount());
    clay_list_add(list, array);
    err = clay_shift(scop, ClayBeta(transformation.target()), transformation.depth(), list, m_options);
    clay_list_free(list); // List cleans inner arrays, too.
  }
    break;
  case Transformation::Kind::Reorder:
    clay_reorder(scop, ClayBeta(transformation.target()), ClayBeta(transformation.order()), m_options);
    break;
  case Transformation::Kind::Skew:
  {
    clay_list_p list = clay_list_malloc();
    clay_array_p constArray = clay_array_malloc();
    clay_array_p paramArray = clay_array_malloc();
    clay_array_p varArray   = clay_array_malloc();
    // Clay does not need all the variables, just those used in the transformation.
    for (int i = 0, e = std::max(transformation.depth(), transformation.secondDepth()); i < e; ++i) {
      int value = 0;
      if (i + 1 == transformation.depth()) value = 1;
      else if (i + 1 == transformation.secondDepth()) value = -transformation.constantAmount();
      clay_array_add(varArray, value);
    }
    clay_list_add(list, varArray);
    clay_list_add(list, paramArray);
    clay_list_add(list, constArray);
    err = clay_shift(scop, ClayBeta(transformation.target()), transformation.depth(), list, m_options);
    clay_list_free(list);
  }
    break;
  case Transformation::Kind::IndexSetSplitting:
  {
    clay_list_p list = clay_list_malloc();
    clay_list_add(list, clay_array_clone(ClayBeta(transformation.iterators())));
    clay_list_add(list, clay_array_clone(ClayBeta(transformation.parameters())));
    clay_array_p array = clay_array_malloc();
    clay_array_add(array, transformation.constantAmount());
    clay_list_add(list, array);
    err = clay_iss(scop, ClayBeta(transformation.target()), list, nullptr, m_options);
    clay_list_free(list);
  }
    break;
  case Transformation::Kind::Grain:
    err = clay_grain(scop, ClayBeta(transformation.target()),
                    transformation.depth(),
                    transformation.constantAmount(), m_options);
    break;
  case Transformation::Kind::Reverse:
    err = clay_reverse(scop, ClayBeta(transformation.target()),
                       transformation.depth(), m_options);
    break;
  case Transformation::Kind::Interchange:
    err = clay_interchange(scop, ClayBeta(transformation.target()),
                           transformation.depth(), transformation.secondDepth(),
                           1, m_options);
    break;
  case Transformation::Kind::Tile:
    err = clay_tile(scop, ClayBeta(transformation.target()),
                    transformation.depth(), transformation.depth(),
                    transformation.constantAmount(), 0, m_options);
//    err = clay_stripmine(scop, ClayBeta(transformation.target()),
//                         transformation.depth(), transformation.constantAmount(),
//                         0, m_options);
//    err += clay_interchange(scop, ClayBeta(transformation.target()),
//                            transformation.depth(), transformation.depth() + 1,
//                            0, m_options);
    break;
  default:
    break;
  }
  CLINT_ASSERT(err == 0, "Error during Clay transformation");
  clay_beta_normalize(scop);
}

ClayBetaMapper::ClayBetaMapper(ClintScop *scop) {
  for (ClintStmt *stmt : scop->statements()) {
    for (ClintStmtOccurrence *occurrence : stmt->occurrences()) {
      std::vector<int> beta = occurrence->betaVector();
      m_originalBeta[occurrence] = beta;
      m_originalOccurrences[beta] = occurrence;
    }
  }
  m_updatedBeta = m_originalBeta;
  m_updatedOccurrences = m_originalOccurrences;
}

inline bool isPrefix(const std::vector<int> &prefix, const std::vector<int> &beta, size_t length = -1ull) {
  if (length == -1ull) {
    length = prefix.size();
  }
  if (beta.size() < length + 1) {
    return false;
  }
  if (length > prefix.size()) {
    return false;
  }
  bool result = true;
  for (int i = 0; i < length; i++) {
    result = result && beta[i] == prefix[i];
  }
  return result;
}

void ClayBetaMapper::apply(osl_scop_p scop, const Transformation &transformation) {
//  oslListForeach(scop->statement, [this](osl_statement_p stmt) {
//    oslListForeach(stmt->scattering, [this](osl_relation_p scattering) {
//      std::vector<int> beta = betaExtract(scattering);
//      CLINT_ASSERT(m_originalOccurrences.find(beta) != std::end(m_originalOccurrences),
//                   "A mismatching scop supplied");
//    });
//  });

  m_updatedBeta.clear();
  m_updatedOccurrences.clear();

  switch (transformation.kind()) {
  case Transformation::Kind::Fuse:
  {
    CLINT_ASSERT(transformation.target().size() >= 1, "Cannot fuse root");
    int idx = 0;
    int nextValue = INT_MAX;
    for (auto it : m_originalOccurrences) {
      const std::vector<int> &originalBeta = it.first;
      const std::vector<int> &target = transformation.target();
      if (isPrefix(target, originalBeta)) {
        idx = std::max(idx, originalBeta[target.size()]);
      }
      if (isPrefix(target, originalBeta, target.size() - 1) &&
          originalBeta[target.size() - 1] > target.back()) {
        nextValue = std::min(nextValue, originalBeta[target.size() - 1]);
      }
    }
    CLINT_ASSERT(nextValue != INT_MAX, "Nothing to fuse with");

    for (auto it : m_originalOccurrences) {
      const std::vector<int> &originalBeta = it.first;
      const std::vector<int> &target = transformation.target();
      std::vector<int> updatedBeta(originalBeta);
      if (target.size() - 1 < originalBeta.size() &&
          isPrefix(target, originalBeta, target.size() - 1)){
        if (originalBeta[target.size() - 1] == nextValue) {
          updatedBeta[target.size() - 1]--;
          updatedBeta[target.size()] = ++idx;
        } else if (originalBeta[target.size() - 1] > nextValue) {
          updatedBeta[target.size() - 1]--;
        }
      }
      m_updatedBeta[it.second] = updatedBeta;
    }
  }
    break;
  case Transformation::Kind::Split:
    CLINT_ASSERT(transformation.depth() >= 1, "Cannot split at depth 0");
    CLINT_ASSERT(transformation.target().size() > transformation.depth(), "Split depth overflow");
    for (auto it : m_originalOccurrences) {
      const std::vector<int> &originalBeta = it.first;
      const std::vector<int> &target(transformation.target());
      std::vector<int> updatedBeta(originalBeta);
      if (isPrefix(target, originalBeta, transformation.depth()) &&
          originalBeta[transformation.depth()] > target[transformation.depth()]) {
        CLINT_ASSERT(updatedBeta.size() >= 1, "Cannot split root");
        updatedBeta[transformation.depth() - 1]++;
        updatedBeta[transformation.depth()] -= (target.back() + 1);
      } else if (isPrefix(target, originalBeta, transformation.depth() - 1) &&
                 originalBeta[transformation.depth() - 1] > target[transformation.depth() - 1]) {
        updatedBeta[transformation.depth() - 1]++;
      }
      m_updatedBeta[it.second] = updatedBeta;
    }
    break;
  case Transformation::Kind::Reorder:
  {
    const std::vector<int> &order = transformation.order();
    for (auto it : m_originalOccurrences) {
      const std::vector<int> &originalBeta = it.first;
      const std::vector<int> &target = transformation.target();
      std::vector<int> updatedBeta(originalBeta);
      if (isPrefix(target, originalBeta)) {
        size_t idx = target.size();
        CLINT_ASSERT(order.size() > updatedBeta[idx], "Reorder vector is too short");
        updatedBeta[idx] = order[updatedBeta[idx]];
      }
      m_updatedBeta[it.second] = updatedBeta;
    }
  }
    break;
  case Transformation::Kind::IndexSetSplitting:
  {
    // 1. Count all betas matching the prefix (unoptimal).
    int numberStmt = 0;
    for (auto it : m_originalOccurrences) {
      const std::vector<int> &originalBeta = it.first;
      const std::vector<int> &target = transformation.target();
      if (isPrefix(target, originalBeta)) {
        numberStmt++;
      }
    }
    // 2. Old statements are not affected, only the new added.
    m_updatedBeta = m_originalBeta;
    // 3. Add newly created betas that do not have links to the occurrence yet???
    for (int i = 0; i < numberStmt; i++) {
      std::vector<int> updatedBeta(transformation.target());
      updatedBeta.push_back(numberStmt + i);

      // XXX: wtf method to keep track of occurrences created by iss
      m_updatedBeta[(ClintStmtOccurrence *) m_lastOccurrenceFakePtr++] = updatedBeta;
    }
  }
    break;

  case Transformation::Kind::Tile:
  {
    // FIXME: m_cscop is null here; is it even used?
//    m_updatedBeta = m_originalBeta; // Keep all statements
//    ClintStmtOccurrence *occ = m_cscop->occurrence(transformation.target());
//    std::vector<int> updatedBeta(transformation.target());
//    updatedBeta.insert(std::begin(updatedBeta) + transformation.depth(), 0);
//    m_updatedBeta[occ] = updatedBeta;
    for (auto it : m_originalOccurrences) {
      const std::vector<int> &originalBeta = it.first;
      const std::vector<int> &target = transformation.target();
      std::vector<int> updatedBeta(originalBeta);
      if (isPrefix(target, originalBeta) ||
          target == originalBeta) {
        updatedBeta.insert(std::begin(updatedBeta) + transformation.depth(), 0);
      }
      m_updatedBeta[it.second] = updatedBeta;
    }
  }
    break;

  case Transformation::Kind::Shift:
  case Transformation::Kind::Skew:
  case Transformation::Kind::Grain:
  case Transformation::Kind::Reverse:
  case Transformation::Kind::Interchange:
    // Do not affect beta
    m_updatedBeta = m_originalBeta;
    break;
  }

  for (auto it : m_updatedBeta) {
    m_updatedOccurrences[it.second] = it.first;
  }
}

void ClayBetaMapper::apply(osl_scop_p scop, const TransformationGroup &group) {
  iterativeApply(scop, group.transformations);
}

void ClayBetaMapper::apply(osl_scop_p scop, const TransformationSequence &sequence) {
  iterativeApply(scop, sequence.groups);
}

ClayBetaMapper2::ClayBetaMapper2(ClintScop *scop) {
  for (ClintStmt *stmt : scop->statements()) {
    for (ClintStmtOccurrence *occurrence : stmt->occurrences()) {
      std::vector<int> beta = occurrence->betaVector();
      m_forwardMapping.emplace(beta, beta);
      syncReverseMapping();
    }
  }
}

ClayBetaMapper2::~ClayBetaMapper2() {

}

void ClayBetaMapper2::replaceMappings(Identifier original, Identifier oldModified, Identifier newModified) {
  removeMappings(original, oldModified);
  addMappings(original, newModified);
}

void ClayBetaMapper2::addMappings(Identifier original, Identifier modified) {
  m_forwardMapping.insert(std::make_pair(original, modified));
  m_reverseMapping.insert(std::make_pair(modified, original));
}

template <typename K, typename M>
static void removeMultimapPair(std::multimap<K, M> &map, const K &key, const M &mapped) {
  typename std::multimap<K, M>::iterator beginIt, endIt, foundIt;
  std::tie(beginIt, endIt) = map.equal_range(key);
  foundIt = std::find_if(beginIt, endIt, [mapped] (const typename std::multimap<K, M>::value_type &element) {
    return element.second == mapped;
  });
  if (foundIt == endIt)
    return;
  map.erase(foundIt);
}

void ClayBetaMapper2::removeMappings(Identifier original, Identifier modified) {
  removeMultimapPair(m_forwardMapping, original, modified);
  removeMultimapPair(m_reverseMapping, modified, original);
}

int ClayBetaMapper2::countMatches(Identifier target) {
  return std::count_if(std::begin(m_forwardMapping),
                             std::end(m_forwardMapping),
                [this,target] (typename IdentifierMultiMap::value_type &v) {
    return isPrefix(target, v.first);
  });
}

void ClayBetaMapper2::apply(osl_scop_p scop, const Transformation &transformation) {
  (void) scop;

  switch (transformation.kind()) {
  case Transformation::Kind::Fuse:
  {
    Identifier target = transformation.target();
    int insertOrder = maximumAt(target);
    nextInLoop(target);

    IdentifierMultiMap updatedForwardMapping;
    std::set<Identifier> updatedCreatedMappings;
    for (auto m : m_forwardMapping) {
      Identifier identifier = m.second;
      int matchingLength = partialMatch(target, identifier);
      if (matchingLength == target.size()) {
        prevInLoop(identifier, transformation.depth() - 1);
        changeOrderAt(identifier, insertOrder + orderAt(identifier, transformation.depth()) + 1, transformation.depth());
      } else if (matchingLength == target.size() - 1 && follows(target, identifier)) {
        prevInLoop(identifier, transformation.depth() - 1);
      }

      if (m_createdMappings.find(m.second) != std::end(m_createdMappings)) {
        updatedCreatedMappings.insert(identifier);
      }
      updatedForwardMapping.emplace(m.first, identifier);
    }
    m_forwardMapping = updatedForwardMapping;
    m_createdMappings = updatedCreatedMappings;
    syncReverseMapping();
  }
    break;

  case Transformation::Kind::Split:
  {
    Identifier target = transformation.target();
    IdentifierMultiMap updatedForwardMapping;
    std::set<Identifier> updatedCreatedMappings;
    for (auto m : m_forwardMapping) {
      Identifier identifier = m.second;

      int matchingLength = partialMatch(target, identifier);
      if (matchingLength == transformation.depth() &&
          followsAt(target, identifier, transformation.depth())) {
        nextInLoop(identifier, transformation.depth() - 1);
        changeOrderAt(identifier, orderAt(identifier, transformation.depth()) - orderAt(target, transformation.depth()) - 1 /*-(orderAt(identifier) + 1)*/, transformation.depth());
      } else if (matchingLength == transformation.depth() - 1 &&
                 followsAt(target, identifier, transformation.depth() - 1)) {
        nextInLoop(identifier, transformation.depth() - 1);
      }

      if (m_createdMappings.find(m.second) != std::end(m_createdMappings)) {
        updatedCreatedMappings.insert(identifier);
      }
      updatedForwardMapping.emplace(m.first, identifier);
    }
    m_forwardMapping = updatedForwardMapping;
    m_createdMappings = updatedCreatedMappings;
    syncReverseMapping();
  }
    break;

  case Transformation::Kind::Reorder:
  {
    Identifier target = transformation.target();
    IdentifierMultiMap updatedForwardMapping;
    std::set<Identifier> updatedCreatedMappings;
//    int stmtNb = maximumAt(target) + 1;
//    CLINT_ASSERT(stmtNb == transformation.order().size(),
//                 "Order vector size mismatch");  // This assumes the normalized beta tree.

    std::vector<int> ordering = transformation.order();
    for (auto m : m_forwardMapping) {
      Identifier identifier = m.second;
      if (isPrefix(target, identifier)) {
        int oldOrder = orderAt(identifier, target.size());
        CLINT_ASSERT(oldOrder < ordering.size(),
                     "Reorder transformation vector too short");
        changeOrderAt(identifier, ordering.at(oldOrder), target.size());
      }

      if (m_createdMappings.find(m.second) != std::end(m_createdMappings)) {
        updatedCreatedMappings.insert(identifier);
      }
      updatedForwardMapping.emplace(m.first, identifier);
    }
    m_forwardMapping = updatedForwardMapping;
    m_createdMappings = updatedCreatedMappings;
    syncReverseMapping();
  }
    break;

  case Transformation::Kind::IndexSetSplitting:
  {
    Identifier target = transformation.target();
    int stmtNb = countMatches(target);
    // TOOD: assert statement number are consecutive (or beta structure is normalized)

    IdentifierMultiMap updatedForwardMapping;
    for (auto m : m_forwardMapping) {
      Identifier identifer  = m.second;
      // Insert the original statement.
      updatedForwardMapping.emplace(m.first, identifer);
      if (isPrefix(target, identifer)) {
        appendStmt(identifer, stmtNb++);
        // Insert the iss-ed statement if needed.
        updatedForwardMapping.emplace(m.first, identifer);
        m_createdMappings.insert(identifer);
      }
    }
    m_forwardMapping = updatedForwardMapping;
    syncReverseMapping();
  }
    break;

  case Transformation::Kind::Tile:
  {
    Identifier target = transformation.target();
    IdentifierMultiMap updatedForwardMapping;
    std::set<Identifier> updatedCreatedMappings;

    for (auto m : m_forwardMapping) {
      Identifier identifier = m.second;
      if (isPrefixOrEqual(target, identifier)) {
        createLoop(identifier, transformation.depth());
      }

      if (m_createdMappings.find(m.second) != std::end(m_createdMappings)) {
        updatedCreatedMappings.insert(identifier);
      }
      updatedForwardMapping.emplace(m.first, identifier);
    }
    m_forwardMapping = updatedForwardMapping;
    m_createdMappings = updatedCreatedMappings;
    syncReverseMapping();
  }
    break;

  case Transformation::Kind::Shift:
  case Transformation::Kind::Skew:
  case Transformation::Kind::Grain:
  case Transformation::Kind::Reverse:
  case Transformation::Kind::Interchange:
    // Do not affect beta
    break;

  default:
    CLINT_UNREACHABLE;
  }
}

void ClayBetaMapper2::apply(osl_scop_p scop, const TransformationGroup &group) {
  iterativeApply(group.transformations);
}

void ClayBetaMapper2::apply(osl_scop_p scop, const TransformationSequence &sequence) {
  iterativeApply(sequence.groups);
}

void ClayBetaMapper2::dump(std::ostream &out) const {
  std::set<Identifier> uniqueKeys;
  for (auto it : m_forwardMapping) {
    uniqueKeys.insert(it.first);
  }
  for (auto key : uniqueKeys) {
    IdentifierMultiMap::const_iterator b, e;
    std::tie(b, e) = m_forwardMapping.equal_range(key);

    out << "(";
    std::copy(std::begin(key), std::end(key), std::ostream_iterator<int>(out, ","));
    out << ")  ->  {";
    for (IdentifierMultiMap::const_iterator i = b; i != e; ++i) {
      out << "(";
      std::copy(std::begin(i->second), std::end(i->second), std::ostream_iterator<int>(out, ","));
      out << "), ";
    }
    out << "}" << std::endl;
  }
}

std::vector<int> ClayTransformer::transformedBeta(const std::vector<int> &beta, const Transformation &transformation) {
  std::vector<int> tBeta(beta);
  switch (transformation.kind()) {
  case Transformation::Kind::Fuse:
    if (beta > transformation.target()) {
      tBeta[transformation.depth()] -= 1;
    }
    break;
  default:
    break;
  }
  return std::move(tBeta);
}

std::vector<int> ClayTransformer::originalBeta(const std::vector<int> &beta, const Transformation &transformation) {
  std::vector<int> tBeta(beta);
  switch (transformation.kind()) {
  case Transformation::Kind::Fuse:
    if (beta > transformation.target()) {
      tBeta[transformation.depth()] += 1;
    }
    break;
  default:
    break;
  }
  return std::move(tBeta);
}
