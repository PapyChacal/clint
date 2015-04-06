#include "oslutils.h"
#include "clintstmt.h"
#include "clintstmtoccurrence.h"

#include <functional>

ClintStmtOccurrence::ClintStmtOccurrence(osl_statement_p stmt, const std::vector<int> &betaVector,
                                     ClintStmt *parent) :
  QObject(parent), m_oslStatement(stmt), m_statement(parent) {
  resetOccurrence(stmt, betaVector);
}

ClintStmtOccurrence *ClintStmtOccurrence::split(osl_statement_p stmt, const std::vector<int> &betaVector) {
  ClintStmtOccurrence *occurrence = new ClintStmtOccurrence(stmt, betaVector, m_statement);
  occurrence->m_tilingDimensions = m_tilingDimensions;
  occurrence->m_tileSizes = m_tileSizes;
  return occurrence;
}

void ClintStmtOccurrence::resetOccurrence(osl_statement_p stmt, const std::vector<int> &betaVector) {
  bool differentBeta = (m_betaVector == betaVector);
  bool differentPoints = false;
  std::vector<osl_relation_p> oslScatterings;
  m_betaVector = betaVector;
  m_oslStatement = stmt; // XXX: check if it's okay everywhere
                         // I am not sure that adding the transformed statement is a good idea, but otherwise we cannot call occurrenceChanged/projectOn for this occurrence
                         // Furthermore, storing the statement in ClintStmtOccurrence rather than in ClintStmt is questionable
                         // There is a comment somewhere saying m_oslStatement may go out of sync with transformed scop, this should fix it...

  if (stmt == nullptr) {
    if (m_oslScatterings.size() != 0)
      emit pointsChanged();
    if (differentBeta)
      emit betaChanged();
    return;
  }

  oslListForeach(stmt->scattering, [this,&betaVector,&oslScatterings,&differentPoints](osl_relation_p scattering) {
    if (betaExtract(scattering) == betaVector) {
      oslScatterings.push_back(scattering);
      if (m_oslScatterings.size() == 0)
        differentPoints = true;
      // Check if the scattering relation is equal to any other old scattering relation in this occurrence.
      // If it is not, than this occurrence was indeed affected by the transformation and should send corresponding updates.
      if (!differentPoints) {
        auto found = std::find_if(std::begin(m_oslScatterings), std::end(m_oslScatterings), [scattering](auto it) {
          return osl_relation_equal(it, scattering);
        });
        differentPoints = differentPoints || found == std::end(m_oslScatterings);
      }
    }
  });
  m_oslScatterings.clear();
  m_oslScatterings = oslScatterings;
  CLINT_ASSERT(m_oslScatterings.size() != 0,
               "Trying to create an occurrence for the inexistent beta-vector");

  m_betaVector.reserve(betaVector.size());
  std::copy(std::begin(betaVector), std::end(betaVector), std::back_inserter(m_betaVector));

  if (differentPoints) {
    emit pointsChanged();
  }
  if (differentBeta) {
    emit betaChanged();
  }
}

void ClintStmtOccurrence::resetBetaVector(const std::vector<int> &betaVector) {
  bool differentBeta = (m_betaVector == betaVector);
  m_betaVector = betaVector;

  if (differentBeta)
    emit betaChanged();
}

bool operator < (const ClintStmtOccurrence &lhs, const ClintStmtOccurrence &rhs) {
  return lhs.m_betaVector < rhs.m_betaVector;
}

bool operator ==(const ClintStmtOccurrence &lhs, const ClintStmtOccurrence &rhs) {
  return lhs.m_betaVector == rhs.m_betaVector;
}

int ClintStmtOccurrence::ignoreTilingDim(int dim) const {
  // Ignore projections on tiled dimensions.
  for (int tilingDim : m_tilingDimensions) {
    if (tilingDim > dim) {
      return dim;
    }
    dim++;
  }
  return dim;
}

std::vector<std::vector<int>> ClintStmtOccurrence::projectOn(int horizontalDimIdx, int verticalDimIdx) const {
  CLINT_ASSERT(m_oslScatterings.size() == 1,
               "Multiple scatterings for one occurrence are not supported yet");
  osl_relation_p scattering = m_oslScatterings[0];

  // Transform iterator (alpha only) indices to enumerator (beta-alpha-beta) indices
  // betaDims are found properly iff the dimensionalityChecker assertion holds.
  int horizontalScatDimIdx    = 1 + 2 * horizontalDimIdx;
  int verticalScatDimIdx      = 1 + 2 * verticalDimIdx;
  int horizontalOrigDimIdx   = scattering->nb_output_dims + horizontalDimIdx;
  int verticalOrigDimIdx     = scattering->nb_output_dims + verticalDimIdx;

  horizontalScatDimIdx  = ignoreTilingDim(horizontalScatDimIdx);
  verticalScatDimIdx    = ignoreTilingDim(verticalScatDimIdx);
//  horizontalOrigDimIdx = ignoreTilingDim(horizontalOrigDimIdx);
//  verticalOrigDimIdx   = ignoreTilingDim(verticalOrigDimIdx);

  // Checking if all the relations for the same beta have the same structure.
  // AZ: Not sure if it is theoretically possible: statements with the same beta-vector
  // should normally be in the same part of the domain union and therefore have same
  // number of scattering variables.  If the following assertion ever fails, check the
  // theoretical statement and if it does not hold, perform enumeration separately for
  // each union part.
  // AZ: this holds for SCoPs generated by Clan.
  // TODO: piggyback on oslUtils and Enumerator to pass around the mapping from original iterators
  // to the scattering (indices in applied matrix) since different parts of the domain and scattering
  // relation unions may have different number of input/output dimensions for the codes
  // originating from outside Clan/Clay toolset.
  // AZ: is this even possible without extra information?  Two problematic cases,
  // 1. a1 = i + j, a2 = i - j; no clear mapping between a and i,j
  // 2. stripmine a1 >/< f(i), a2 = i; two a correspond to i in the same time.
  auto dimensionalityCheckerFunction = [](osl_relation_p rel, int output_dims,
                                          int input_dims, int parameters) {
    CLINT_ASSERT(rel->nb_output_dims == output_dims,
                 "Dimensionality mismatch, proper polyhedron construction impossible");
    CLINT_ASSERT(rel->nb_input_dims == input_dims,
                 "Dimensionality mismatch, proper polyhedron construction impossible");
    CLINT_ASSERT(rel->nb_parameters == parameters,
                 "Dimensionality mismatch, proper polyhedron construction impossible");
  };
  oslListForeach(m_oslStatement->domain, dimensionalityCheckerFunction, m_oslStatement->domain->nb_output_dims,
                 m_oslStatement->domain->nb_input_dims, m_oslStatement->domain->nb_parameters);
  std::for_each(std::begin(m_oslScatterings), std::end(m_oslScatterings),
                std::bind(dimensionalityCheckerFunction, std::placeholders::_1,
                          m_oslScatterings.front()->nb_output_dims,
                          m_oslScatterings.front()->nb_input_dims,
                          m_oslScatterings.front()->nb_parameters));

  bool horizontalOrigDimValid = horizontalOrigDimIdx < scattering->nb_output_dims
      + m_oslStatement->domain->nb_output_dims;
  bool verticalOrigDimValid = verticalOrigDimIdx < scattering->nb_output_dims
      + m_oslStatement->domain->nb_output_dims;

  // Get original and scattered iterator values depending on the axis displayed.
  std::vector<int> visibleDimensions;
  bool projectHorizontal = (horizontalDimIdx != -2) && (dimensionality() > horizontalDimIdx); // FIXME: -2 is in VizProperties::NO_DIMENSION, but should be in a different class since it has nothing to do with viz
  bool projectVertical   = (verticalDimIdx != -2) && (dimensionality() > verticalDimIdx);
  CLINT_ASSERT(!(projectHorizontal ^ (horizontalScatDimIdx >= 0 && horizontalScatDimIdx < scattering->nb_output_dims)),
               "Trying to project to the horizontal dimension that is not present in scattering");
  CLINT_ASSERT(!(projectVertical ^ (verticalScatDimIdx >= 0 && verticalScatDimIdx < scattering->nb_output_dims)),
               "Trying to project to the vertical dimension that is not present in scattering");

  // Deal with stripmine cases in inner ifs.
  // TODO: use a structure with optional<int> for each point rather than plain vector
  // it would allow to differentiate cases where some values are not present and why;
  // namely 2 scattered for 1 original in case of stripmine vs 1 scattered for 2 original in case of flatten
  if (!projectHorizontal && !projectVertical) {
    // This is just a point, no actual enumeration needed.
    // All the dimensions being projected out, the result of enumeration is a single zero-dimensional point.
  } else if (projectHorizontal && !projectVertical) {
    visibleDimensions.push_back(horizontalScatDimIdx);
    if (horizontalOrigDimValid)
      visibleDimensions.push_back(horizontalOrigDimIdx);
  } else if (!projectHorizontal && projectVertical) {
    visibleDimensions.push_back(verticalScatDimIdx);
    if (verticalOrigDimValid)
      visibleDimensions.push_back(verticalOrigDimIdx);
  } else {
    visibleDimensions.push_back(horizontalScatDimIdx);
    visibleDimensions.push_back(verticalScatDimIdx);
    if (horizontalOrigDimValid)
      visibleDimensions.push_back(horizontalOrigDimIdx);
    if (verticalOrigDimValid)
      visibleDimensions.push_back(verticalOrigDimIdx);
  }

  osl_relation_p applied = oslApplyScattering(oslListToVector(m_oslStatement->domain),
                                              m_oslScatterings);
  osl_relation_p ready = oslRelationsWithContext(applied, m_statement->scop()->fixedContext());

  std::vector<std::vector<int>> points =
      program()->enumerator()->enumerate(ready, std::move(visibleDimensions));
  computeMinMax(points,
                horizontalDimIdx != -2 ? depth(horizontalDimIdx) - 1 : horizontalDimIdx,
                verticalDimIdx != -2 ? depth(verticalDimIdx) - 1 : verticalDimIdx);

  return std::move(points);
}

void ClintStmtOccurrence::computeMinMax(const std::vector<std::vector<int>> &points,
                                      int horizontalDimIdx, int verticalDimIdx) const {
  // Initialize with extreme values for min and max unless already computed for previous polyhedron
  int horizontalMin, horizontalMax, verticalMin = 0, verticalMax = 0;
  if (horizontalDimIdx == -2) { // FIXME: -2 is in VizProperties::NO_DIMENSION
    return;
  }
  if (points.size() == 0) {
    m_cachedDimMins[horizontalDimIdx] = 0;
    m_cachedDimMaxs[horizontalDimIdx] = 0;
    m_cachedDimMins[verticalDimIdx] = 0;
    m_cachedDimMaxs[verticalDimIdx] = 0;
    return;
  }
  size_t pointSize = points.front().size();
  switch (pointSize) {
  case 4:
  case 3: // FIXME: does not take into accout possible flatten
    verticalMin   = INT_MAX;
    verticalMax   = INT_MIN;
    // fall through
  case 2:
    horizontalMin = INT_MAX;
    horizontalMax = INT_MIN;
    break;
  case 1:
    CLINT_ASSERT(false, "Trying to project on the dimension that was flattened.");
    break;
  case 0:
    return;
    break;
  default:
    CLINT_UNREACHABLE;
    break;
  }

  // Compute min and max values for projected iterators of the current coordinate system.
  for (const std::vector<int> &point : points) {
    CLINT_ASSERT(point.size() == pointSize,
                 "Enumerated points have different dimensionality");
    horizontalMin = std::min(horizontalMin, point[0]);
    horizontalMax = std::max(horizontalMax, point[0]);
    if (pointSize >= 3) {
      verticalMin = std::min(verticalMin, point[1]);
      verticalMax = std::max(verticalMax, point[1]);
    }
  }
  CLINT_ASSERT(horizontalMin != INT_MIN, "Could not compute horizontal minimum");
  CLINT_ASSERT(horizontalMax != INT_MAX, "Could not compute horizontal maximum");
  CLINT_ASSERT(verticalMin != INT_MIN, "Could not compute vertical minimum");
  CLINT_ASSERT(verticalMax != INT_MAX, "Could not compute vertical maximum");

  m_cachedDimMins[horizontalDimIdx] = horizontalMin;
  m_cachedDimMaxs[horizontalDimIdx] = horizontalMax;
  if (pointSize >= 3) {
    m_cachedDimMins[verticalDimIdx] = verticalMin;
    m_cachedDimMaxs[verticalDimIdx] = verticalMax;
  }
}

static inline void negateBonudlikeForm(std::vector<int> &form) {
  if (form.size() <= 2)
    return;
  for (size_t i = 1; i < form.size() - 1; i++) {
    form[i] = -form[i];
  }
  form.back() = -form.back() - 1;
}

std::vector<int> ClintStmtOccurrence::makeBoundlikeForm(Bound bound, int dimIdx, int constValue,
                                                        int constantBoundaryPart,
                                                        const std::vector<int> &parameters,
                                                        const std::vector<int> &parameterValues) {
  bool isConstantForm = std::all_of(std::begin(parameters), std::end(parameters),
                                    std::bind(std::equal_to<int>(), std::placeholders::_1, 0));
  // Substitute parameters with current values.
  // Constant will differ depending on the target form:
  // for the constant form, all parameters are replaced with respective values;
  // for the parametric form, adjust with the current constant part of the boundary.
  int parameterSubstValue = 0;
  for (size_t i = 0; i < parameters.size(); i++) {
    parameterSubstValue += parameters[i] * parameterValues[i];
  }
  parameterSubstValue += constantBoundaryPart;
  if (bound == Bound::LOWER && isConstantForm) {
    constValue = constValue - 1;
  } else if (bound == Bound::UPPER && isConstantForm) {
    constValue = -constValue - 1;
  } else if (bound == Bound::LOWER && !isConstantForm) {
    constValue = -constantBoundaryPart + (constValue + parameterSubstValue - 1);
  } else if (bound == Bound::UPPER && !isConstantForm) {
    constValue = -constantBoundaryPart + (parameterSubstValue - constValue - 1);
  } else {
    CLINT_UNREACHABLE;
  }

  std::vector<int> form;
  form.push_back(1); // Inequality
  for (int i = 0; i < m_oslStatement->domain->nb_output_dims; i++) {
    form.push_back(i == dimIdx ? (bound == Bound::LOWER ? -1 : 1) : 0);
  }
  for (int p : parameters) {
    form.push_back(-p);
  }
  form.push_back(constValue);
  return std::move(form);
}

static bool isVariableBoundary(int row, osl_relation_p scattering, int dimIdx) {
  bool variable = false;
  for (int i = 0; i < scattering->nb_input_dims + scattering->nb_output_dims + scattering->nb_local_dims; i++) {
    if (i + 1 == dimIdx)
      continue;
    if (!osl_int_zero(scattering->precision, scattering->m[row][1 + i]))
      variable = true;
  }

  return variable;
}

static bool isParametricBoundary(int row, osl_relation_p scattering) {
  int firstParameterDim = scattering->nb_input_dims +
      scattering->nb_output_dims +
      scattering->nb_local_dims + 1;
  for (int i = 0; i < scattering->nb_parameters; i++) {
    if (!osl_int_zero(scattering->precision, scattering->m[row][firstParameterDim + i]))
      return true;
  }
  return false;
}

std::vector<int> ClintStmtOccurrence::findBoundlikeForm(Bound bound, int dimIdx, int constValue) {
  CLINT_ASSERT(m_oslScatterings.size() == 1, "Cannot find a form for multiple scatterings at the same time");

  std::vector<int> zeroParameters(m_oslStatement->domain->nb_parameters, 0);
  std::vector<int> parameterValues = scop()->parameterValues();

  int oslDimIdx = 1 + dimIdx * 2 + 1;
  osl_relation_p scheduledDomain = ISLEnumerator::scheduledDomain(m_oslStatement->domain, m_oslScatterings.front());
  int lowerRow = oslRelationDimUpperBound(scheduledDomain, oslDimIdx);
  int upperRow = oslRelationDimLowerBound(scheduledDomain, oslDimIdx);

  // Check if the request boundary exists and is unique.
  // If failed, try the opposite boundary.  If the opposite does not
  // exist or is not unique, use constant form.
  if (lowerRow < 0 && upperRow < 0) {
    return makeBoundlikeForm(bound, dimIdx, constValue, 0, zeroParameters, parameterValues);
  } else if (lowerRow < 0 && bound == Bound::LOWER) {
    bound = Bound::UPPER;
  } else if (upperRow < 0 && bound == Bound::UPPER) {
    bound = Bound::LOWER;
  }

  // Check if the current boundary (either request or opposite) is
  // variable.  If it is, try the opposite unless the opposite does
  // not exist.  If both are variable or one is variable and another
  // does not exit, use constant form.
  bool upperVariable = upperRow >= 0 ? isVariableBoundary(upperRow, scheduledDomain, oslDimIdx) : true;
  bool lowerVariable = lowerRow >= 0 ? isVariableBoundary(lowerRow, scheduledDomain, oslDimIdx) : true;
  if (upperVariable && lowerVariable) {
    return makeBoundlikeForm(bound, dimIdx, constValue, 0, zeroParameters, parameterValues);
  }
  if (upperVariable && lowerRow >= 0) {
    bound = Bound::LOWER;
  } else if (lowerVariable && upperRow >= 0) {
    bound = Bound::UPPER;
  }
  int row = bound == Bound::UPPER ? upperRow : lowerRow;

  int firstParameterIdx = 1 + scheduledDomain->nb_input_dims + scheduledDomain->nb_output_dims + scheduledDomain->nb_local_dims;
  std::vector<int> parameters;
  parameters.reserve(scheduledDomain->nb_parameters);
  for (int i = 0; i < scheduledDomain->nb_parameters; i++) {
    int p = osl_int_get_si(scheduledDomain->precision, scheduledDomain->m[row][firstParameterIdx + i]);
    parameters.push_back(p);
  }

  // Make parametric form is the boundary itself is parametric.
  // Make constant form otherwise.
  if (isParametricBoundary(row, scheduledDomain)) {
    int boundaryConstantPart = osl_int_get_si(scheduledDomain->precision,
                                              scheduledDomain->m[row][scheduledDomain->nb_columns - 1]);
    return makeBoundlikeForm(bound, dimIdx, constValue,
                             boundaryConstantPart,
                             parameters, parameterValues);
  } else {
    return makeBoundlikeForm(bound, dimIdx, constValue,
                             0, zeroParameters, parameterValues);
  }
}

int ClintStmtOccurrence::minimumValue(int dimIdx) const {
  if (dimIdx >= dimensionality() || dimIdx < 0)
    return 0;
  if (m_cachedDimMins.count(dimIdx) == 0) {
    projectOn(dimIdx, INT_MAX);
  }
  CLINT_ASSERT(m_cachedDimMins.count(dimIdx) == 1,
               "min cache failure");
  return m_cachedDimMins[dimIdx];
}

int ClintStmtOccurrence::maximumValue(int dimIdx) const {
  if (dimIdx >= dimensionality() || dimIdx < 0)
    return 0;
  if (m_cachedDimMaxs.count(dimIdx) == 0) {
    projectOn(dimIdx, INT_MAX);
  }
  CLINT_ASSERT(m_cachedDimMaxs.count(dimIdx) == 1,
               "max cache failure");
  return m_cachedDimMaxs[dimIdx];
}

std::vector<int> ClintStmtOccurrence::untiledBetaVector() const {
  std::vector<int> beta(m_betaVector);
  // m_tilingDimensions is an ordered set, start from the end to remove higher
  // indices from beta-vector first.  Thus lower indices will remain the same.
  for (auto it = m_tilingDimensions.rbegin(), eit = m_tilingDimensions.rend();
       it != eit; it++) {
    if ((*it) % 2 == 0) {
      int index = (*it) / 2;
      beta.erase(beta.begin() + index);
    }
  }
  return std::move(beta);
}

static void updateMinMaxCache(std::unordered_map<int, int> &cache, int dimensionIdx) {
  std::unordered_map<int, int> updated;
  for (auto v : cache) {
    if (v.first >= dimensionIdx) {
      updated[v.first + 1] = v.second;
    } else {
      updated[v.first] = v.second;
    }
  }
  cache = updated;
}

void ClintStmtOccurrence::tile(int dimensionIdx, unsigned tileSize) {
  CLINT_ASSERT(tileSize != 0,
               "Cannot tile by 0 elements");

  // Ignore previously tiled dimensions.
  dimensionIdx = depth(dimensionIdx) - 1;

  std::set<int> tilingDimensions;
  std::unordered_map<int, unsigned> tileSizes;
  for (int dim : m_tilingDimensions) {
    if (dim >= 2 * dimensionIdx) {
      tilingDimensions.insert(dim + 2);
      if (m_tileSizes.count(dim))
        tileSizes[dim + 2] = m_tileSizes[dim];
    } else {
      tilingDimensions.insert(dim);
      if (m_tileSizes.count(dim))
        tileSizes[dim] = m_tileSizes[dim];
    }
  }
  tilingDimensions.insert(2 * dimensionIdx);
  tilingDimensions.insert(2 * dimensionIdx + 1);
  tileSizes[2 * dimensionIdx + 1] = tileSize;
  m_tilingDimensions = tilingDimensions;
  m_tileSizes = tileSizes;

  // Update min/max caches wrt to new dimensionality.
  updateMinMaxCache(m_cachedDimMaxs, dimensionIdx);
  updateMinMaxCache(m_cachedDimMins, dimensionIdx);
}
