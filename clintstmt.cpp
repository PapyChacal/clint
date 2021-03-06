#include "enumerator.h"
#include "macros.h"
#include "oslutils.h"
#include "clintstmt.h"
#include "clintstmtoccurrence.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

inline void fillDimensionNames(char **strings, std::vector<std::string> &dimensionNames) {
  for (char **str = strings; str != nullptr && *str != nullptr; str++) {
    dimensionNames.emplace_back(*str);
  }
}

ClintStmt::ClintStmt(osl_statement_p stmt, ClintScop *parent) :
  QObject(parent), m_scop(parent) {

  // FIXME: redoing iteration over scattering union parts
  oslListForeach(stmt->scattering, [this,stmt](osl_relation_p scattering) {
    std::vector<int> betaVector = betaExtract(scattering);
    if (m_occurrences.count(betaVector) == 0) {
      makeOccurrence(stmt, betaVector);
    }
  });

  // Get dimension names for the statement.
  //   Try to find iterator names in either body or extbody.
  void *bodyGeneric = osl_generic_lookup(stmt->extension, OSL_URI_BODY);
  void *extbodyGeneric = osl_generic_lookup(stmt->extension, OSL_URI_EXTBODY);
  void *namesGeneric = osl_generic_lookup(m_scop->scopPart()->extension, OSL_URI_SCATNAMES);
  osl_body_p body = nullptr;
  if (bodyGeneric) {
    body = static_cast<osl_body_p>(bodyGeneric);
  } else if (extbodyGeneric) {
    osl_extbody_p extbody = static_cast<osl_extbody_p>(extbodyGeneric);
    body = extbody->body;
  }

  //   If iterators found, use them.  Create default names otherwise.
  // FIXME(osl): we have multiple places where iterator names are placed
  // body, extbody's body and scatnames.
  if (namesGeneric) {
    osl_strings_p betaIterators = static_cast<osl_scatnames_p>(namesGeneric)->names;
    for (int i = 1, e = osl_strings_size(betaIterators); i < e; i += 2) {
      m_dimensionNames.emplace_back(betaIterators->string[i]);
    }
  } else if (body && body->iterators) {
    fillDimensionNames(body->iterators->string, m_dimensionNames);
  } else {
    int nbIterators = 0;
    oslListForeach(stmt->domain, [&nbIterators](osl_relation_p domain) mutable {
      nbIterators = std::max(nbIterators, domain->nb_output_dims);
    });
    // Create at least 1 name of each type to avoid weird memory allocations.
    static char letters[] = "piyzl"; // Have modifiable chars so that C++11 compiler does not complain on old osl C function.
    osl_names_p names = osl_names_generate(&letters[0], 1, &letters[1], nbIterators, &letters[2], 1, &letters[3], 1, &letters[4], 1);
    fillDimensionNames(names->iterators->string, m_dimensionNames);
    osl_names_free(names);
  }
}

void ClintStmt::updateBetas(const std::map<std::vector<int>, std::vector<int> > &mapping) {
  std::map<std::vector<int>, ClintStmtOccurrence *> updatedBetas;
  std::set<std::vector<int>> keysToRemove;
  for (auto it : m_occurrences) {
    if (mapping.count(it.first) == 1) {
      updatedBetas[mapping.at(it.first)] = it.second;
      keysToRemove.insert(it.first);
    }
  }
  for (auto it : keysToRemove) {
    m_occurrences.erase(it);
  }
  for (auto it : updatedBetas) {
    m_occurrences.insert(it);
  }
}

std::vector<ClintStmtOccurrence *> ClintStmt::occurrences() const {
  std::vector<ClintStmtOccurrence *> result;
  for (auto occurence : m_occurrences) {
    result.push_back(occurence.second);
  }
  std::sort(std::begin(result), std::end(result), VizStmtOccurrencePtrComparator());
  return std::move(result);
}

ClintStmtOccurrence *ClintStmt::makeOccurrence(osl_statement_p stmt, const std::vector<int> &beta) {
  ClintStmtOccurrence *occurrence = new ClintStmtOccurrence(stmt, beta, this);
  m_occurrences[beta] = occurrence;
  return occurrence;
}

ClintStmtOccurrence *ClintStmt::splitOccurrence(ClintStmtOccurrence *occurrence, const std::vector<int> &beta) {
  ClintStmtOccurrence *otherOccurrence = occurrence->split(beta);
  m_occurrences[beta] = otherOccurrence;
  return otherOccurrence;
}

void ClintStmt::removeOccurrence(ClintStmtOccurrence *occurrence) {
  const std::vector<int> beta = occurrence->betaVector();
  m_occurrences.erase(beta);
  delete occurrence;
}
