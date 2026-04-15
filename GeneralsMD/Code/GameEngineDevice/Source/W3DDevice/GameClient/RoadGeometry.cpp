// RoadGeometry.cpp
// Self-contained road geometry generation ported from W3DRoadBuffer.
// Zero DX8/WW3D dependencies -- pure geometry + height queries.
// Original author: John Ahlquist, May 2001. Ported April 2026.

#include "W3DDevice/GameClient/RoadGeometry.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "Common/MapObject.h"
#include "Common/GlobalData.h"
#include "GameClient/TerrainRoads.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace RoadGeometry {

// =========================================================================
// LineSeg helpers
// =========================================================================

Vec3 LineSeg::Find_Point_Closest_To(const Vec3& pos) const {
    Vec3 ab = P1 - P0;
    Vec3 ap = pos - P0;
    float t = (ap.X*ab.X + ap.Y*ab.Y + ap.Z*ab.Z) /
              (ab.X*ab.X + ab.Y*ab.Y + ab.Z*ab.Z + 1e-20f);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return Vec3(P0.X + ab.X*t, P0.Y + ab.Y*t, P0.Z + ab.Z*t);
}

bool LineSeg::Find_Intersection(const LineSeg& other, Vec3* p1, float* f1, Vec3* p2, float* f2) const {
    // 2D line-line intersection (Z ignored), same as WW3D LineSegClass
    Vec3 d1 = P1 - P0;
    Vec3 d2 = other.P1 - other.P0;
    float denom = d1.X * d2.Y - d1.Y * d2.X;
    if (fabsf(denom) < 1e-10f) return false;
    Vec3 d0 = other.P0 - P0;
    float t = (d0.X * d2.Y - d0.Y * d2.X) / denom;
    float s = (d0.X * d1.Y - d0.Y * d1.X) / denom;
    if (f1) *f1 = t;
    if (f2) *f2 = s;
    if (p1) {
        p1->X = P0.X + d1.X * t;
        p1->Y = P0.Y + d1.Y * t;
        p1->Z = 0;
    }
    if (p2) {
        p2->X = other.P0.X + d2.X * s;
        p2->Y = other.P0.Y + d2.Y * s;
        p2->Z = 0;
    }
    return true;
}

// =========================================================================
// WorldHeightMapAdapter
// =========================================================================

float WorldHeightMapAdapter::getMaxCellHeight(float x, float y) const {
    if (!m_map) return 0.0f;
    int borderSize = m_map->getBorderSizeInline();
    int iX = (int)(x / ROAD_MAP_XY_FACTOR);
    int iY = (int)(y / ROAD_MAP_XY_FACTOR);
    iX += borderSize;
    iY += borderSize;
    if (iX < 0) iX = 0;
    if (iY < 0) iY = 0;
    if (iX >= (m_map->getXExtent()-1)) iX = m_map->getXExtent()-2;
    if (iY >= (m_map->getYExtent()-1)) iY = m_map->getYExtent()-2;
    unsigned char* data = m_map->getDataPtr();
    int w = m_map->getXExtent();
    float p0 = data[iX     + iY     * w] * ROAD_HEIGHT_SCALE;
    float p1 = data[(iX+1) + iY     * w] * ROAD_HEIGHT_SCALE;
    float p2 = data[(iX+1) + (iY+1) * w] * ROAD_HEIGHT_SCALE;
    float p3 = data[iX     + (iY+1) * w] * ROAD_HEIGHT_SCALE;
    float h = p0;
    if (p1 > h) h = p1;
    if (p2 > h) h = p2;
    if (p3 > h) h = p3;
    return h;
}

int WorldHeightMapAdapter::getMaxHeightValue() const { return 255; } // K_MAX_HEIGHT
int WorldHeightMapAdapter::getMinHeightValue() const { return 0; }   // K_MIN_HEIGHT

// =========================================================================
// RoadSegment
// =========================================================================

RoadSegment::RoadSegment()
    : m_curveRadius(0.0f), m_type(SEGMENT), m_scale(1.0f),
      m_widthInTexture(1.0f), m_uniqueID(0), m_visible(false),
      m_numVertex(0), m_vb(nullptr), m_numIndex(0), m_ib(nullptr)
{
}

RoadSegment::~RoadSegment() {
    delete[] m_vb;
    delete[] m_ib;
}

RoadSegment::RoadSegment(const RoadSegment& o)
    : m_pt1(o.m_pt1), m_pt2(o.m_pt2), m_curveRadius(o.m_curveRadius),
      m_type(o.m_type), m_scale(o.m_scale), m_widthInTexture(o.m_widthInTexture),
      m_uniqueID(o.m_uniqueID), m_visible(o.m_visible),
      m_numVertex(0), m_vb(nullptr), m_numIndex(0), m_ib(nullptr), m_info(o.m_info)
{
    if (o.m_numVertex > 0 && o.m_vb) {
        m_vb = new RoadVertex[o.m_numVertex];
        memcpy(m_vb, o.m_vb, o.m_numVertex * sizeof(RoadVertex));
        m_numVertex = o.m_numVertex;
    }
    if (o.m_numIndex > 0 && o.m_ib) {
        m_ib = new uint16_t[o.m_numIndex];
        memcpy(m_ib, o.m_ib, o.m_numIndex * sizeof(uint16_t));
        m_numIndex = o.m_numIndex;
    }
}

RoadSegment& RoadSegment::operator=(const RoadSegment& o) {
    if (this == &o) return *this;
    m_pt1 = o.m_pt1; m_pt2 = o.m_pt2;
    m_curveRadius = o.m_curveRadius; m_type = o.m_type;
    m_scale = o.m_scale; m_widthInTexture = o.m_widthInTexture;
    m_uniqueID = o.m_uniqueID; m_visible = o.m_visible;
    m_info = o.m_info;

    delete[] m_vb; m_vb = nullptr; m_numVertex = 0;
    delete[] m_ib; m_ib = nullptr; m_numIndex = 0;

    if (o.m_numVertex > 0 && o.m_vb) {
        m_vb = new RoadVertex[o.m_numVertex];
        memcpy(m_vb, o.m_vb, o.m_numVertex * sizeof(RoadVertex));
        m_numVertex = o.m_numVertex;
    }
    if (o.m_numIndex > 0 && o.m_ib) {
        m_ib = new uint16_t[o.m_numIndex];
        memcpy(m_ib, o.m_ib, o.m_numIndex * sizeof(uint16_t));
        m_numIndex = o.m_numIndex;
    }
    return *this;
}

void RoadSegment::SetVertexBuffer(RoadVertex* vb, int numVertex) {
    delete[] m_vb;
    m_vb = nullptr;
    m_numVertex = 0;
    if (numVertex < 1 || numVertex > MAX_SEG_VERTEX) return;
    m_vb = new RoadVertex[numVertex];
    if (!m_vb) return;
    m_numVertex = numVertex;
    memcpy(m_vb, vb, numVertex * sizeof(RoadVertex));
}

void RoadSegment::SetIndexBuffer(uint16_t* ib, int numIndex) {
    delete[] m_ib;
    m_ib = nullptr;
    m_numIndex = 0;
    if (numIndex < 1 || numIndex > MAX_SEG_INDEX) return;
    m_ib = new uint16_t[numIndex];
    if (!m_ib) return;
    m_numIndex = numIndex;
    memcpy(m_ib, ib, numIndex * sizeof(uint16_t));
}

int RoadSegment::GetVertices(RoadVertex* dst, int n) const {
    if (!m_vb || n < 1 || n > m_numVertex) return 0;
    memcpy(dst, m_vb, n * sizeof(RoadVertex));
    return n;
}

int RoadSegment::GetIndices(uint16_t* dst, int n, int offset) const {
    if (!m_ib || n < 1 || n > m_numIndex) return 0;
    for (int i = 0; i < n; i++) {
        dst[i] = m_ib[i] + (uint16_t)offset;
    }
    return n;
}

// =========================================================================
// Road Builder (internal class holding all the pipeline state)
// =========================================================================

class RoadBuilder {
public:
    RoadBuilder(IHeightMap* hm);
    ~RoadBuilder();

    RoadMeshOutput build();

private:
    IHeightMap*     m_heightMap;
    RoadSegment*    m_roads;
    int             m_numRoads;
    int             m_maxRoadSegments;
    int             m_maxRoadVertex;
    int             m_maxRoadIndex;
    int             m_maxRoadTypes;
    RoadTypeInfo*   m_roadTypes;

    // Helpers
    static int  xpSign(const Vec2& v1, const Vec2& v2);
    void rotateAbout(Vec2* ptP, Vec2 center, float angle);
    void flipTheRoad(RoadSegment* pRoad);

    // Pipeline
    void addMapObjects();
    void addMapObject(RoadSegment* pRoad, bool updateTheCounts);
    void updateCounts(RoadSegment* pRoad);
    void updateCountsAndFlags();
    void checkLinkBefore(int ndx);
    void checkLinkAfter(int ndx);
    void moveRoadSegTo(int fromNdx, int toNdx);
    void miter(int ndx1, int ndx2);
    void insertCurveSegmentAt(int ndx1, int ndx2);
    void insertCurveSegments();
    void insertTee(Vec2 loc, int index1, float scale);
    bool insertY(Vec2 loc, int index1, float scale);
    void insert4Way(Vec2 loc, int index1, float scale);
    void insertTeeIntersections();
    void offset3Way(TRoadPt* pc1, TRoadPt* pc2, TRoadPt* pc3, Vec2 loc, Vec2 upVector, Vec2 teeVector, float widthInTexture);
    void offsetH(TRoadPt* pc1, TRoadPt* pc2, TRoadPt* pc3, Vec2 loc, Vec2 upVector, Vec2 teeVector, bool flip, bool mirror, float widthInTexture);
    void offsetY(TRoadPt* pc1, TRoadPt* pc2, TRoadPt* pc3, Vec2 loc, Vec2 upVector, float widthInTexture);
    void offset4Way(TRoadPt* pc1, TRoadPt* pc2, TRoadPt* pc3, TRoadPt* pr3, TRoadPt* pc4, Vec2 loc, Vec2 alignVector, float widthInTexture);
    void insertCrossTypeJoins();
    int  findCrossTypeJoinVector(Vec2 loc, Vec2* joinVector, int uniqueID);
    void adjustStacking(int topUniqueID, int bottomUniqueID);

    // Vertex generation
    void preloadRoadsInVertexAndIndexBuffers();
    void preloadRoadSegment(RoadSegment* pRoad);
    void loadFloatSection(RoadSegment* pRoad, Vec2 loc, Vec2 roadVector, float halfHeight, float left, float right, float uOffset, float vOffset, float scale);
    void loadFloat4PtSection(RoadSegment* pRoad, Vec2 loc, Vec2 roadNormal, Vec2 roadVector, Vec2* cornersP, float uOffset, float vOffset, float uScale, float vScale);
    void loadCurve(RoadSegment* pRoad, Vec2 loc1, Vec2 loc2, float scale);
    void loadTee(RoadSegment* pRoad, Vec2 loc1, Vec2 loc2, bool is4Way, float scale);
    void loadY(RoadSegment* pRoad, Vec2 loc1, Vec2 loc2, float scale);
    void loadH(RoadSegment* pRoad, Vec2 loc1, Vec2 loc2, bool flip, float scale);
    void loadAlphaJoin(RoadSegment* pRoad, Vec2 loc1, Vec2 loc2, float scale);

    // Batching
    RoadMeshOutput gatherBatches();
};

// Macro from original -- bounds check before adding a road segment
#define CHECK_SEGMENTS { if (m_numRoads >= m_maxRoadSegments) { return; } }

// =========================================================================
// RoadBuilder implementation
// =========================================================================

RoadBuilder::RoadBuilder(IHeightMap* hm)
    : m_heightMap(hm), m_roads(nullptr), m_numRoads(0),
      m_roadTypes(nullptr)
{
    m_maxRoadSegments = TheGlobalData ? TheGlobalData->m_maxRoadSegments : 500;
    m_maxRoadVertex   = TheGlobalData ? TheGlobalData->m_maxRoadVertex   : 1000;
    m_maxRoadIndex    = TheGlobalData ? TheGlobalData->m_maxRoadIndex    : 2000;
    m_maxRoadTypes    = TheGlobalData ? TheGlobalData->m_maxRoadTypes    : 8;

    m_roads = new RoadSegment[m_maxRoadSegments];

    // Load road types from INI
    m_roadTypes = new RoadTypeInfo[m_maxRoadTypes];
    if (TheTerrainRoads) {
        int i = 0;
        for (TerrainRoadType* road = TheTerrainRoads->firstRoad(); road && i < m_maxRoadTypes;
             road = TheTerrainRoads->nextRoad(road))
        {
            m_roadTypes[i].uniqueID          = road->getID();
            m_roadTypes[i].roadWidth         = road->getRoadWidth();
            m_roadTypes[i].roadWidthInTexture= road->getRoadWidthInTexture();
            m_roadTypes[i].textureName       = road->getTexture().str();
            i++;
        }
    }
}

RoadBuilder::~RoadBuilder() {
    delete[] m_roads;
    delete[] m_roadTypes;
}

// ---- xpSign (line 115 of original) ----
int RoadBuilder::xpSign(const Vec2& v1, const Vec2& v2) {
    float xpdct = v1.X * v2.Y - v1.Y * v2.X;
    if (xpdct < 0) return -1;
    if (xpdct > 0) return 1;
    return 0;
}

// ---- rotateAbout (line 3036 of original) ----
void RoadBuilder::rotateAbout(Vec2* ptP, Vec2 center, float angle) {
    Vec2 offset;
    offset.X = ptP->X - center.X;
    offset.Y = ptP->Y - center.Y;
    Vec2 orgOffset = offset;
    offset.Rotate(angle);
    offset.Y -= orgOffset.Y;
    offset.X -= orgOffset.X;
    *ptP += offset;
}

void RoadBuilder::flipTheRoad(RoadSegment* pRoad) {
    TRoadPt tmp = pRoad->m_pt1;
    pRoad->m_pt1 = pRoad->m_pt2;
    pRoad->m_pt2 = tmp;
}

// =========================================================================
// addMapObject (line 1506)
// =========================================================================
void RoadBuilder::addMapObject(RoadSegment* pRoad, bool updateTheCounts) {
    RoadSegment cur = *pRoad;
    Vec2 loc1, loc2;
    loc1 = cur.m_pt1.loc;
    loc2 = cur.m_pt2.loc;
    Vec2 roadVector(loc2.X - loc1.X, loc2.Y - loc1.Y);
    Vec2 roadNormal(-roadVector.Y, roadVector.X);
    roadNormal.Normalize();
    roadNormal *= (cur.m_scale * cur.m_widthInTexture / 2.0f);
    cur.m_pt1.top = loc1 + roadNormal;
    cur.m_pt1.bottom = loc1 - roadNormal;
    cur.m_pt2.top = loc2 + roadNormal;
    cur.m_pt2.bottom = loc2 - roadNormal;

    if (updateTheCounts) {
        updateCounts(&cur);
    }

    CHECK_SEGMENTS;
    int i;
    bool flip = false;
    bool addBefore = false;
    bool addAfter = false;
    bool bothMatch = false;
    for (i = 0; i < m_numRoads; i++) {
        bothMatch = false;
        if ((m_roads[i].m_pt1.loc == loc1 && m_roads[i].m_pt2.loc == loc2) ||
            (m_roads[i].m_pt1.loc == loc2 && m_roads[i].m_pt2.loc == loc1)) {
            bothMatch = true;
            break;
        }
        if (cur.m_pt1.count == 1) {
            if (m_roads[i].m_pt1.loc == loc1) {
                flip = true;
                addAfter = true;
            }
            if (m_roads[i].m_pt2.loc == loc1) {
                flip = false;
                addBefore = true;
            }
        }
        if (cur.m_pt2.count == 1) {
            if (m_roads[i].m_pt1.loc == loc2) {
                flip = false;
                addAfter = true;
            }
            if (m_roads[i].m_pt2.loc == loc2) {
                flip = true;
                addBefore = true;
            }
        }
        if (addBefore || addAfter) {
            break;
        }
    }
    if (bothMatch) {
        return;
    }
    int addIndex = i;
    if (addAfter) {
        addIndex++;
    }
    if (addIndex < m_numRoads) {
        for (i = m_numRoads; i > addIndex; i--) {
            m_roads[i] = m_roads[i - 1];
        }
    }

    m_roads[addIndex] = cur;
    if (flip) {
        flipTheRoad(&m_roads[addIndex]);
    }
    m_numRoads++;
    if (addBefore) {
        checkLinkBefore(addIndex);
    } else if (addAfter) {
        checkLinkAfter(addIndex);
    }
}

// =========================================================================
// addMapObjects (line 1593)
// =========================================================================
void RoadBuilder::addMapObjects() {
    MapObject* pMapObj;
    MapObject* pMapObj2;
    for (pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext()) {
        if (m_numRoads >= m_maxRoadSegments) {
            break;
        }
        if (pMapObj->getFlag(FLAG_ROAD_POINT1)) {
            pMapObj2 = pMapObj->getNext();
            if (pMapObj2 == nullptr) break;
            if (!pMapObj2->getFlag(FLAG_ROAD_POINT2)) continue;
            Vec2 loc1, loc2;
            loc1.Set(pMapObj->getLocation()->x, pMapObj->getLocation()->y);
            loc2.Set(pMapObj2->getLocation()->x, pMapObj2->getLocation()->y);
            if (loc1.X == loc2.X && loc1.Y == loc2.Y) {
                loc2.X += 0.25f;
            }
            RoadSegment curRoad;
            curRoad.m_scale = DEFAULT_ROAD_SCALE;
            curRoad.m_widthInTexture = 1.0f;
            curRoad.m_uniqueID = 1;
            bool found = false;
            if (TheTerrainRoads) {
                TerrainRoadType* road = TheTerrainRoads->findRoad(pMapObj->getName());
                if (road) {
                    curRoad.m_widthInTexture = road->getRoadWidthInTexture();
                    curRoad.m_scale = road->getRoadWidth();
                    curRoad.m_uniqueID = road->getID();
                    found = true;
                }
            }
            curRoad.m_pt1.loc = loc1;
            curRoad.m_pt1.isAngled = pMapObj->getFlag(FLAG_ROAD_CORNER_ANGLED);
            curRoad.m_pt1.isJoin = pMapObj->getFlag(FLAG_ROAD_JOIN);
            curRoad.m_pt2.loc = loc2;
            curRoad.m_pt2.isAngled = pMapObj2->getFlag(FLAG_ROAD_CORNER_ANGLED);
            curRoad.m_pt2.isJoin = pMapObj2->getFlag(FLAG_ROAD_JOIN);
            curRoad.m_type = SEGMENT;
            curRoad.m_curveRadius = pMapObj->getFlag(FLAG_ROAD_CORNER_TIGHT) ? TIGHT_CORNER_RADIUS : CORNER_RADIUS;

            addMapObject(&curRoad, true);
            pMapObj = pMapObj2;
        }
    }
    int curCount = m_numRoads;
    int i;
    m_numRoads = 0;
    for (i = 0; i < curCount; i++) {
        RoadSegment curRoad = m_roads[i];
        addMapObject(&curRoad, false);
    }
}

// =========================================================================
// updateCounts (line 1675)
// =========================================================================
void RoadBuilder::updateCounts(RoadSegment* pRoad) {
    pRoad->m_pt1.last = true;
    pRoad->m_pt2.last = true;
    pRoad->m_pt1.multi = false;
    pRoad->m_pt2.multi = false;
    pRoad->m_pt1.count = 0;
    pRoad->m_pt2.count = 0;
    Vec2 loc1, loc2;
    loc1 = pRoad->m_pt1.loc;
    loc2 = pRoad->m_pt2.loc;
    int i;
    for (i = 0; i < m_numRoads; i++) {
        if (m_roads[i].m_uniqueID != pRoad->m_uniqueID) {
            continue;
        }
        if (m_roads[i].m_pt1.loc == loc1) {
            m_roads[i].m_pt1.count++;
            pRoad->m_pt1.count++;
        }
        if (m_roads[i].m_pt1.loc == loc2) {
            m_roads[i].m_pt1.count++;
            pRoad->m_pt2.count++;
        }
        if (m_roads[i].m_pt2.loc == loc1) {
            m_roads[i].m_pt2.count++;
            pRoad->m_pt1.count++;
        }
        if (m_roads[i].m_pt2.loc == loc2) {
            m_roads[i].m_pt2.count++;
            pRoad->m_pt2.count++;
        }
        m_roads[i].m_pt1.multi = m_roads[i].m_pt1.count > 1;
        m_roads[i].m_pt2.multi = m_roads[i].m_pt2.count > 1;
    }
    pRoad->m_pt1.multi = pRoad->m_pt1.count > 1;
    pRoad->m_pt2.multi = pRoad->m_pt2.count > 1;
}

// =========================================================================
// updateCountsAndFlags (line 1720)
// =========================================================================
void RoadBuilder::updateCountsAndFlags() {
    int i, j;
    for (i = 0; i < m_numRoads; i++) {
        m_roads[i].m_pt1.last = true;
        m_roads[i].m_pt2.last = true;
        m_roads[i].m_pt1.count = 0;
        m_roads[i].m_pt2.count = 0;
    }
    for (j = m_numRoads - 1; j > 0; j--) {
        Vec2 loc1, loc2;
        loc1 = m_roads[j].m_pt1.loc;
        loc2 = m_roads[j].m_pt2.loc;
        for (i = 0; i < j; i++) {
            if (m_roads[i].m_uniqueID != m_roads[j].m_uniqueID) {
                continue;
            }
            if (m_roads[i].m_pt1.loc == loc1) {
                m_roads[i].m_pt1.last = false;
                m_roads[i].m_pt1.count++;
                m_roads[j].m_pt1.count++;
            }
            if (m_roads[i].m_pt1.loc == loc2) {
                m_roads[i].m_pt1.last = false;
                m_roads[i].m_pt1.count++;
                m_roads[j].m_pt2.count++;
            }
            if (m_roads[i].m_pt2.loc == loc1) {
                m_roads[i].m_pt2.last = false;
                m_roads[i].m_pt2.count++;
                m_roads[j].m_pt1.count++;
            }
            if (m_roads[i].m_pt2.loc == loc2) {
                m_roads[i].m_pt2.last = false;
                m_roads[i].m_pt2.count++;
                m_roads[j].m_pt2.count++;
            }
        }
    }
}

// =========================================================================
// moveRoadSegTo (line 1367)
// =========================================================================
void RoadBuilder::moveRoadSegTo(int fromNdx, int toNdx) {
    if (fromNdx < 0 || fromNdx >= m_numRoads || toNdx < 0 || toNdx >= m_numRoads) {
        return;
    }
    if (fromNdx == toNdx) return;
    RoadSegment cur = m_roads[fromNdx];
    int i;
    if (fromNdx < toNdx) {
        for (i = fromNdx; i < toNdx; i++) {
            m_roads[i] = m_roads[i + 1];
        }
    } else {
        for (i = fromNdx; i > toNdx; i--) {
            m_roads[i] = m_roads[i - 1];
        }
    }
    m_roads[toNdx] = cur;
}

// =========================================================================
// checkLinkBefore (line 1395)
// =========================================================================
void RoadBuilder::checkLinkBefore(int ndx) {
    if (m_roads[ndx].m_pt2.count != 1) {
        return;
    }
    Vec2 loc2 = m_roads[ndx].m_pt2.loc;
    int endOfCurSeg = ndx + 1;
    while (endOfCurSeg < m_numRoads - 1) {
        if (m_roads[endOfCurSeg].m_pt1.loc != m_roads[endOfCurSeg + 1].m_pt2.loc) {
            break;
        }
        endOfCurSeg++;
    }
    int checkNdx = endOfCurSeg + 1;
    while (checkNdx < m_numRoads) {
        if (m_roads[checkNdx].m_pt1.loc == loc2) {
            moveRoadSegTo(checkNdx, ndx);
            loc2 = m_roads[ndx].m_pt2.loc;
            if (m_roads[ndx].m_pt2.count != 1) return;
            endOfCurSeg++;
        } else if (m_roads[checkNdx].m_pt2.loc == loc2) {
            flipTheRoad(&m_roads[checkNdx]);
            moveRoadSegTo(checkNdx, ndx);
            loc2 = m_roads[ndx].m_pt2.loc;
            if (m_roads[ndx].m_pt2.count != 1) return;
            endOfCurSeg++;
        } else {
            checkNdx++;
        }
        if (checkNdx <= endOfCurSeg) {
            checkNdx = endOfCurSeg + 1;
        }
    }
}

// =========================================================================
// checkLinkAfter (line 1454)
// =========================================================================
void RoadBuilder::checkLinkAfter(int ndx) {
    if (m_roads[ndx].m_pt1.count != 1) {
        return;
    }
    if (ndx >= m_numRoads - 1) {
        return;
    }
    Vec2 loc1 = m_roads[ndx].m_pt1.loc;
    int checkNdx = ndx + 1;
    while (checkNdx < m_numRoads && ndx < m_numRoads - 1) {
        if (m_roads[checkNdx].m_pt2.loc == loc1) {
            ndx++;
            moveRoadSegTo(checkNdx, ndx);
            loc1 = m_roads[ndx].m_pt1.loc;
            if (m_roads[ndx].m_pt1.count != 1) return;
        } else if (m_roads[checkNdx].m_pt1.loc == loc1) {
            flipTheRoad(&m_roads[checkNdx]);
            ndx++;
            moveRoadSegTo(checkNdx, ndx);
            loc1 = m_roads[ndx].m_pt1.loc;
            if (m_roads[ndx].m_pt1.count != 1) return;
        } else {
            checkNdx++;
        }
    }
}

// =========================================================================
// miter (line 2832)
// =========================================================================
void RoadBuilder::miter(int ndx1, int ndx2) {
    Vec3 p1 = Vec3(m_roads[ndx1].m_pt1.top.X, m_roads[ndx1].m_pt1.top.Y, 0);
    Vec3 p2 = Vec3(m_roads[ndx1].m_pt2.top.X, m_roads[ndx1].m_pt2.top.Y, 0);
    LineSeg offsetLine1(p1, p2);
    p1 = Vec3(m_roads[ndx2].m_pt1.top.X, m_roads[ndx2].m_pt1.top.Y, 0);
    p2 = Vec3(m_roads[ndx2].m_pt2.top.X, m_roads[ndx2].m_pt2.top.Y, 0);
    LineSeg offsetLine2(p1, p2);
    Vec3 pInt1, pInt2;
    float nu;
    if (offsetLine1.Find_Intersection(offsetLine2, &pInt1, &nu, &pInt2, &nu)) {
        m_roads[ndx2].m_pt2.top.X = pInt1.X;
        m_roads[ndx2].m_pt2.top.Y = pInt1.Y;
        m_roads[ndx1].m_pt1.top.X = pInt1.X;
        m_roads[ndx1].m_pt1.top.Y = pInt1.Y;
    }
    p1 = Vec3(m_roads[ndx1].m_pt1.bottom.X, m_roads[ndx1].m_pt1.bottom.Y, 0);
    p2 = Vec3(m_roads[ndx1].m_pt2.bottom.X, m_roads[ndx1].m_pt2.bottom.Y, 0);
    offsetLine1 = LineSeg(p1, p2);
    p1 = Vec3(m_roads[ndx2].m_pt1.bottom.X, m_roads[ndx2].m_pt1.bottom.Y, 0);
    p2 = Vec3(m_roads[ndx2].m_pt2.bottom.X, m_roads[ndx2].m_pt2.bottom.Y, 0);
    offsetLine2 = LineSeg(p1, p2);
    if (offsetLine1.Find_Intersection(offsetLine2, &pInt1, &nu, &pInt2, &nu)) {
        m_roads[ndx2].m_pt2.bottom.X = pInt1.X;
        m_roads[ndx2].m_pt2.bottom.Y = pInt1.Y;
        m_roads[ndx1].m_pt1.bottom.X = pInt1.X;
        m_roads[ndx1].m_pt1.bottom.Y = pInt1.Y;
    }
}

// =========================================================================
// insertCurveSegmentAt (line 2868)
// =========================================================================
void RoadBuilder::insertCurveSegmentAt(int ndx1, int ndx2) {
    const float DOT_LIMIT = 0.5f;
    float radius = m_roads[ndx1].m_curveRadius * m_roads[ndx1].m_scale;
    Vec2 originalPt = m_roads[ndx1].m_pt1.loc;

    LineSeg line1(Vec3(m_roads[ndx1].m_pt1.loc.X, m_roads[ndx1].m_pt1.loc.Y, 0),
                  Vec3(m_roads[ndx1].m_pt2.loc.X, m_roads[ndx1].m_pt2.loc.Y, 0));
    LineSeg line2(Vec3(m_roads[ndx2].m_pt1.loc.X, m_roads[ndx2].m_pt1.loc.Y, 0),
                  Vec3(m_roads[ndx2].m_pt2.loc.X, m_roads[ndx2].m_pt2.loc.Y, 0));
    Vec2* pr1; Vec2* pr2; Vec2* pr3; Vec2* pr4;

    if (m_roads[ndx1].m_uniqueID != m_roads[ndx2].m_uniqueID) {
        return;
    }
    float curSin = Vec3::Dot_Product(line1.Get_Dir(), line2.Get_Dir());
    float xpdct = Vec3::Cross_Product_Z(line1.Get_Dir(), line2.Get_Dir());
    bool turnRight;
    if (xpdct > 0) {
        pr1 = &m_roads[ndx1].m_pt1.loc;
        pr2 = &m_roads[ndx1].m_pt2.loc;
        pr3 = &m_roads[ndx2].m_pt1.loc;
        pr4 = &m_roads[ndx2].m_pt2.loc;
        turnRight = true;
    } else {
        pr4 = &m_roads[ndx1].m_pt1.loc;
        pr3 = &m_roads[ndx1].m_pt2.loc;
        pr2 = &m_roads[ndx2].m_pt1.loc;
        pr1 = &m_roads[ndx2].m_pt2.loc;
        turnRight = false;
        line1.Set(Vec3(pr1->X, pr1->Y, 0), Vec3(pr2->X, pr2->Y, 0));
        line2.Set(Vec3(pr3->X, pr3->Y, 0), Vec3(pr4->X, pr4->Y, 0));
    }
    float angle_val = acosf(std::max(-1.0f, std::min(1.0f, curSin)));
    float count = angle_val / (PI_F / 6.0f);
    if (count < 0.9f || m_roads[ndx1].m_pt1.isAngled) {
        miter(ndx1, ndx2);
        return;
    }

    Vec3 offset1(radius * line1.Get_Dir().X, radius * line1.Get_Dir().Y, radius * line1.Get_Dir().Z);
    Vec3 offset2(radius * line2.Get_Dir().X, radius * line2.Get_Dir().Y, radius * line2.Get_Dir().Z);
    offset1.Rotate_Z(-PI_F / 2);
    offset2.Rotate_Z(-PI_F / 2);

    Vec3 p1 = Vec3(pr1->X, pr1->Y, 0) + offset1;
    Vec3 p2 = Vec3(pr2->X, pr2->Y, 0) + offset1;
    LineSeg offsetLine1(p1, p2);
    p1 = Vec3(pr3->X, pr3->Y, 0) + offset2;
    p2 = Vec3(pr4->X, pr4->Y, 0) + offset2;
    LineSeg offsetLine2(p1, p2);
    Vec3 pInt1, pInt2;
    Vec3 pInt3, pInt4;
    float nu;
    if (offsetLine1.Find_Intersection(offsetLine2, &pInt1, &nu, &pInt2, &nu)) {
        m_roads[ndx2].m_pt2.last = true;
        LineSeg cross1(pInt1, pInt1 - offset2);
        LineSeg cross2(pInt1, pInt1 - offset1);
        cross1.Find_Intersection(line2, &pInt1, &nu, &pInt2, &nu);
        cross2.Find_Intersection(line1, &pInt3, &nu, &pInt4, &nu);
        // Make sure the lines didn't clip out of existence.
        float theDot = Vec3::Dot_Product(line2.Get_Dir(), pInt1 - Vec3(pr3->X, pr3->Y, 0));
        if (theDot < DOT_LIMIT) {
            *pr1 = originalPt;
            *pr4 = originalPt;
            miter(ndx1, ndx2);
            return;
        }
        theDot = Vec3::Dot_Product(line1.Get_Dir(), Vec3(pr2->X, pr2->Y, 0) - pInt3);
        if (theDot < DOT_LIMIT) {
            *pr1 = originalPt;
            *pr4 = originalPt;
            miter(ndx1, ndx2);
            return;
        }
        *pr4 = Vec2(pInt1.X, pInt1.Y);
        float curveAngle = -PI_F / 6.0f;
        Vec2 pt2 = *pr4;
        Vec2 pt1 = *pr3;
        Vec2 direction(pt1.X - pt2.X, pt1.Y - pt2.Y);
        Vec2 centerOfCurve(-direction.Y, direction.X);
        centerOfCurve.Normalize();
        centerOfCurve *= m_roads[ndx1].m_curveRadius * m_roads[ndx1].m_scale;
        centerOfCurve += pt2;

        rotateAbout(&pt2, centerOfCurve, curveAngle);
        direction.Rotate(curveAngle);
        pt1 = pt2 + direction;

        m_roads[m_numRoads].m_pt1.loc = pt2;
        m_roads[m_numRoads].m_pt2.loc = pt1;

        CHECK_SEGMENTS;
        m_roads[m_numRoads].m_pt1.last = true;
        m_roads[m_numRoads].m_pt2.last = true;
        m_roads[m_numRoads].m_scale = m_roads[ndx1].m_scale;
        m_roads[m_numRoads].m_widthInTexture = m_roads[ndx1].m_widthInTexture;
        m_roads[m_numRoads].m_type = CURVE;
        m_roads[m_numRoads].m_curveRadius = m_roads[ndx1].m_curveRadius;
        m_roads[m_numRoads].m_uniqueID = m_roads[ndx1].m_uniqueID;
        m_numRoads++;
        if (count > 2.0f) {
            int i;
            for (i = 2; (float)i < count; i++) {
                direction.Rotate(curveAngle);
                rotateAbout(&pt2, centerOfCurve, curveAngle);
                pt1 = pt2 + direction;
                CHECK_SEGMENTS;
                m_roads[m_numRoads].m_pt1.loc.Set(pt2);
                m_roads[m_numRoads].m_pt2.loc.Set(pt1);
                m_roads[m_numRoads].m_pt1.last = true;
                m_roads[m_numRoads].m_pt2.last = true;
                m_roads[m_numRoads].m_scale = m_roads[ndx1].m_scale;
                m_roads[m_numRoads].m_widthInTexture = m_roads[ndx1].m_widthInTexture;
                m_roads[m_numRoads].m_type = CURVE;
                m_roads[m_numRoads].m_curveRadius = m_roads[ndx1].m_curveRadius;
                m_roads[m_numRoads].m_uniqueID = m_roads[ndx1].m_uniqueID;
                m_numRoads++;
            }
        }

        *pr1 = Vec2(pInt3.X, pInt3.Y);

        m_roads[ndx1].m_pt1.last = true;
        if (count > 1.0f) {
            pt2 = *pr1;
            pt1 = *pr1 + *pr1 - *pr2;
            direction.Set(pt1.X - pt2.X, pt1.Y - pt2.Y);
            pt1 = pt2 + direction;
            CHECK_SEGMENTS;
            m_roads[m_numRoads].m_pt1.loc.Set(pt2);
            m_roads[m_numRoads].m_pt2.loc.Set(pt1);
            m_roads[m_numRoads].m_pt1.last = true;
            m_roads[m_numRoads].m_pt2.last = true;
            m_roads[m_numRoads].m_scale = m_roads[ndx1].m_scale;
            m_roads[m_numRoads].m_widthInTexture = m_roads[ndx1].m_widthInTexture;
            m_roads[m_numRoads].m_type = CURVE;
            m_roads[m_numRoads].m_curveRadius = m_roads[ndx1].m_curveRadius;
            m_roads[m_numRoads].m_uniqueID = m_roads[ndx1].m_uniqueID;
            m_numRoads++;
        }

        // Recalculate top & bottom.
        Vec2 roadVector = m_roads[ndx1].m_pt2.loc - m_roads[ndx1].m_pt1.loc;
        Vec2 roadNormal(-roadVector.Y, roadVector.X);
        roadNormal.Normalize();
        roadNormal *= (m_roads[ndx1].m_scale * m_roads[ndx1].m_widthInTexture / 2.0f);
        m_roads[ndx1].m_pt1.top = m_roads[ndx1].m_pt1.loc + roadNormal;
        m_roads[ndx1].m_pt1.bottom = m_roads[ndx1].m_pt1.loc - roadNormal;

        roadVector = m_roads[ndx2].m_pt2.loc - m_roads[ndx2].m_pt1.loc;
        roadNormal = Vec2(-roadVector.Y, roadVector.X);
        roadNormal.Normalize();
        roadNormal *= (m_roads[ndx2].m_scale * m_roads[ndx2].m_widthInTexture / 2.0f);
        m_roads[ndx2].m_pt2.top = m_roads[ndx2].m_pt2.loc + roadNormal;
        m_roads[ndx2].m_pt2.bottom = m_roads[ndx2].m_pt2.loc - roadNormal;
    }
}

// =========================================================================
// insertCurveSegments (line 2614)
// =========================================================================
void RoadBuilder::insertCurveSegments() {
    int numRoadSegments = m_numRoads;
    int i;
    int segmentStartIndex = -1;
    for (i = 0; i < numRoadSegments; i++) {
        if (i < numRoadSegments - 1 && m_roads[i].m_pt1.loc == m_roads[i + 1].m_pt2.loc) {
            if (m_roads[i + 1].m_pt2.count == 1 && m_roads[i].m_pt1.count == 1) {
                insertCurveSegmentAt(i, i + 1);
                if (segmentStartIndex < 0) {
                    segmentStartIndex = i;
                }
            }
        } else if (segmentStartIndex >= 0) {
            if (m_roads[i].m_pt1.loc == m_roads[segmentStartIndex].m_pt2.loc) {
                if (m_roads[segmentStartIndex].m_pt2.count == 1 && m_roads[i].m_pt1.count == 1) {
                    insertCurveSegmentAt(i, segmentStartIndex);
                }
            }
            segmentStartIndex = -1;
        }
    }
}

// =========================================================================
// insertTee (line 1767)
// =========================================================================
void RoadBuilder::insertTee(Vec2 loc, int index1, float scale) {
    if (insertY(loc, index1, scale)) {
        return;
    }

    TRoadPt* pr1 = nullptr; TRoadPt* pr2 = nullptr; TRoadPt* pr3 = nullptr;
    TRoadPt* pc1 = nullptr; TRoadPt* pc2 = nullptr; TRoadPt* pc3 = nullptr;

    if (m_roads[index1].m_pt1.loc == loc) {
        pr1 = &m_roads[index1].m_pt2;
        pc1 = &m_roads[index1].m_pt1;
    } else {
        pr1 = &m_roads[index1].m_pt1;
        pc1 = &m_roads[index1].m_pt2;
    }
    int i;
    int index2 = 0;
    int index3 = 0;
    for (i = index1 + 1; i < m_numRoads; i++) {
        if (m_roads[i].m_pt1.loc == loc) {
            m_roads[i].m_pt1.count = -2;
            if (pr2 == nullptr) {
                pr2 = &m_roads[i].m_pt2; pc2 = &m_roads[i].m_pt1; index2 = i;
            } else {
                pr3 = &m_roads[i].m_pt2; pc3 = &m_roads[i].m_pt1; index3 = i;
            }
        }
        if (m_roads[i].m_pt2.loc == loc) {
            m_roads[i].m_pt2.count = -2;
            if (pr2 == nullptr) {
                pr2 = &m_roads[i].m_pt1; pc2 = &m_roads[i].m_pt2; index2 = i;
            } else {
                pr3 = &m_roads[i].m_pt1; pc3 = &m_roads[i].m_pt2; index3 = i;
            }
        }
    }
    if (pr2 == nullptr || pr3 == nullptr) {
        return;
    }

    Vec2 v1 = pr1->loc - loc; v1.Normalize();
    Vec2 v2 = pr2->loc - loc; v2.Normalize();
    Vec2 v3 = pr3->loc - loc; v3.Normalize();
    float dot12 = Vec2::Dot_Product(v1, v2);
    float dot13 = Vec2::Dot_Product(v1, v3);
    float dot32 = Vec2::Dot_Product(v3, v2);
    bool do12 = false, do13 = false, do32 = false;

    if (dot12 < dot13) {
        if (dot12 < dot32) do12 = true; else do32 = true;
    } else {
        if (dot13 < dot32) do13 = true; else do32 = true;
    }

    Vec2 upVector;
    Vec2 decider;
    if (do12) { upVector = v2 - v1; decider = v3; }
    if (do13) { upVector = v3 - v1; decider = v2; }
    if (do32) { upVector = v2 - v3; decider = v1; }
    upVector.Normalize();

    const float cos60 = 0.5f;
    float dot = fabsf(Vec2::Dot_Product(upVector, decider));
    if (dot > cos60) {
        // H-shaped tee
        float angle_val = (PI_F / 2);
        float xp = Vec3::Cross_Product_Z(Vec3(upVector.X, upVector.Y, 0), Vec3(decider.X, decider.Y, 0));
        bool mirror = false;
        if (xp < 0) { angle_val = -angle_val; mirror = true; }
        upVector.Normalize();
        upVector *= 0.5f * scale;
        Vec2 teeVector(upVector);
        teeVector.Rotate(angle_val);

        bool flip;
        if (do12) {
            flip = xpSign(teeVector, v3) == 1;
            offsetH(pc1, pc2, pc3, loc, upVector, teeVector, flip, mirror, m_roads[index1].m_widthInTexture);
        }
        if (do13) {
            flip = xpSign(teeVector, v2) == 1;
            offsetH(pc1, pc3, pc2, loc, upVector, teeVector, flip, mirror, m_roads[index1].m_widthInTexture);
        }
        if (do32) {
            flip = xpSign(teeVector, v1) == 1;
            offsetH(pc3, pc2, pc1, loc, upVector, teeVector, flip, mirror, m_roads[index1].m_widthInTexture);
        }

        pc1->last = true; pc1->count = 0;
        pc2->last = true; pc2->count = 0;
        pc3->last = true; pc3->count = 0;

        CHECK_SEGMENTS;
        m_roads[m_numRoads].m_pt1.loc.Set(loc);
        m_roads[m_numRoads].m_pt2.loc.Set(loc + teeVector);
        m_roads[m_numRoads].m_pt1.last = true;
        m_roads[m_numRoads].m_pt2.last = true;
        m_roads[m_numRoads].m_scale = m_roads[index1].m_scale;
        m_roads[m_numRoads].m_widthInTexture = m_roads[index1].m_widthInTexture;
        m_roads[m_numRoads].m_pt1.count = -3;
        m_roads[m_numRoads].m_type = flip ? THREE_WAY_H_FLIP : THREE_WAY_H;
        m_roads[m_numRoads].m_uniqueID = m_roads[index1].m_uniqueID;
        m_numRoads++;
    } else {
        // Standard tee
        float angle_val = (PI_F / 2);
        float xp = Vec3::Cross_Product_Z(Vec3(upVector.X, upVector.Y, 0), Vec3(decider.X, decider.Y, 0));
        if (xp < 0) angle_val = -angle_val;
        upVector.Normalize();
        upVector *= 0.5f * scale;
        Vec2 teeVector(upVector);
        teeVector.Rotate(angle_val);

        if (do12) { offset3Way(pc1, pc2, pc3, loc, upVector, teeVector, m_roads[index1].m_widthInTexture); }
        if (do13) { offset3Way(pc1, pc3, pc2, loc, upVector, teeVector, m_roads[index1].m_widthInTexture); }
        if (do32) { offset3Way(pc3, pc2, pc1, loc, upVector, teeVector, m_roads[index1].m_widthInTexture); }
        pc1->last = true; pc1->count = 0;
        pc2->last = true; pc2->count = 0;
        pc3->last = true; pc3->count = 0;

        CHECK_SEGMENTS;
        m_roads[m_numRoads].m_pt1.loc.Set(loc);
        m_roads[m_numRoads].m_pt2.loc.Set(loc + teeVector);
        m_roads[m_numRoads].m_pt1.last = true;
        m_roads[m_numRoads].m_pt2.last = true;
        m_roads[m_numRoads].m_scale = m_roads[index1].m_scale;
        m_roads[m_numRoads].m_widthInTexture = m_roads[index1].m_widthInTexture;
        m_roads[m_numRoads].m_pt1.count = -3;
        m_roads[m_numRoads].m_type = TEE;
        m_roads[m_numRoads].m_uniqueID = m_roads[index1].m_uniqueID;
        m_numRoads++;
    }
}

// =========================================================================
// insertY (line 1966)
// =========================================================================
bool RoadBuilder::insertY(Vec2 loc, int index1, float scale) {
    TRoadPt* pr1 = nullptr; TRoadPt* pr2 = nullptr; TRoadPt* pr3 = nullptr;
    TRoadPt* pc1 = nullptr; TRoadPt* pc2 = nullptr; TRoadPt* pc3 = nullptr;

    if (m_roads[index1].m_pt1.loc == loc) {
        pr1 = &m_roads[index1].m_pt2; pc1 = &m_roads[index1].m_pt1;
    } else {
        pr1 = &m_roads[index1].m_pt1; pc1 = &m_roads[index1].m_pt2;
    }
    int i;
    int index2 = 0, index3 = 0;
    for (i = index1 + 1; i < m_numRoads; i++) {
        if (m_roads[i].m_pt1.loc == loc) {
            m_roads[i].m_pt1.count = -2;
            if (pr2 == nullptr) { pr2 = &m_roads[i].m_pt2; pc2 = &m_roads[i].m_pt1; index2 = i; }
            else { pr3 = &m_roads[i].m_pt2; pc3 = &m_roads[i].m_pt1; index3 = i; }
        }
        if (m_roads[i].m_pt2.loc == loc) {
            m_roads[i].m_pt2.count = -2;
            if (pr2 == nullptr) { pr2 = &m_roads[i].m_pt1; pc2 = &m_roads[i].m_pt2; index2 = i; }
            else { pr3 = &m_roads[i].m_pt1; pc3 = &m_roads[i].m_pt2; index3 = i; }
        }
    }
    if (pr2 == nullptr || pr3 == nullptr) return false;

    Vec2 v1 = pr1->loc - loc; v1.Normalize();
    Vec2 v2 = pr2->loc - loc; v2.Normalize();
    Vec2 v3 = pr3->loc - loc; v3.Normalize();

    bool do12 = false, do13 = false, do32 = false;
    float dot12 = Vec2::Dot_Product(v1, v2);
    float dot13 = Vec2::Dot_Product(v1, v3);
    float dot32 = Vec2::Dot_Product(v3, v2);
    float score12 = 2.0f, score13 = 2.0f, score32 = 2.0f;

    const float cos30 = 0.866f;
    const float cos45 = 0.707f;

    if (dot12 < (-cos30)) return false;
    if (dot13 < (-cos30)) return false;
    if (dot32 < (-cos30)) return false;

    int s2 = xpSign(v1, v2);
    int s3 = xpSign(v1, v3);

    if (s2 != s3 && (s2 + s3 == 0)) {
        Vec2 v1_90(-v1.Y, v1.X);
        if (xpSign(v1_90, v2) == 1 && xpSign(v1_90, v3) == 1) {
            do32 = true;
            score32 = fabsf(dot12 + cos45) + fabsf(dot13 + cos45);
        }
    }

    int s1 = xpSign(v3, v1);
    s2 = xpSign(v3, v2);
    if (s2 != s1 && (s2 + s1 == 0)) {
        Vec2 v3_90(-v3.Y, v3.X);
        if (xpSign(v3_90, v2) == 1 && xpSign(v3_90, v1) == 1) {
            do12 = true;
            score12 = fabsf(dot13 + cos45) + fabsf(dot32 + cos45);
        }
    }

    s1 = xpSign(v2, v1);
    s3 = xpSign(v2, v3);
    if (s3 != s1 && (s3 + s1 == 0)) {
        Vec2 v2_90(-v2.Y, v2.X);
        if (xpSign(v2_90, v3) == 1 && xpSign(v2_90, v1) == 1) {
            do13 = true;
            score13 = fabsf(dot12 + cos45) + fabsf(dot32 + cos45);
        }
    }

    if (score12 < score13) {
        do13 = false;
        if (score12 < score32) do32 = false; else do12 = false;
    } else {
        do12 = false;
        if (score13 < score32) do32 = false; else do13 = false;
    }

    Vec2 upVector;
    if (do12) { upVector = v3; }
    else if (do13) { upVector = v2; }
    else if (do32) { upVector = v1; }
    else { return false; }

    float angle_val = -(PI_F / 2);
    upVector.Normalize();
    upVector *= 0.5f * scale;
    Vec2 teeVector(upVector);
    teeVector.Rotate(angle_val);

    if (do12) {
        if (xpSign(v3, v1) == -1) offsetY(pc1, pc2, pc3, loc, upVector, m_roads[index1].m_widthInTexture);
        else offsetY(pc2, pc1, pc3, loc, upVector, m_roads[index1].m_widthInTexture);
    }
    if (do13) {
        if (xpSign(v2, v1) == -1) offsetY(pc1, pc3, pc2, loc, upVector, m_roads[index1].m_widthInTexture);
        else offsetY(pc3, pc1, pc2, loc, upVector, m_roads[index1].m_widthInTexture);
    }
    if (do32) {
        if (xpSign(v1, v3) == -1) offsetY(pc3, pc2, pc1, loc, upVector, m_roads[index1].m_widthInTexture);
        else offsetY(pc2, pc3, pc1, loc, upVector, m_roads[index1].m_widthInTexture);
    }

    pc1->last = true; pc1->count = 0;
    pc2->last = true; pc2->count = 0;
    pc3->last = true; pc3->count = 0;

    if (m_numRoads >= m_maxRoadSegments) return false;
    m_roads[m_numRoads].m_pt1.loc.Set(loc);
    m_roads[m_numRoads].m_pt2.loc.Set(loc + teeVector);
    m_roads[m_numRoads].m_pt1.last = true;
    m_roads[m_numRoads].m_pt2.last = true;
    m_roads[m_numRoads].m_scale = m_roads[index1].m_scale;
    m_roads[m_numRoads].m_widthInTexture = m_roads[index1].m_widthInTexture;
    m_roads[m_numRoads].m_pt1.count = -3;
    m_roads[m_numRoads].m_type = THREE_WAY_Y;
    m_roads[m_numRoads].m_uniqueID = m_roads[index1].m_uniqueID;
    m_numRoads++;

    return true;
}

// =========================================================================
// offset3Way (line 2171)
// =========================================================================
void RoadBuilder::offset3Way(TRoadPt* pc1, TRoadPt* pc2, TRoadPt* pc3,
                             Vec2 loc, Vec2 upVector, Vec2 teeVector, float widthInTexture)
{
    pc1->loc = loc - upVector;
    pc2->loc = loc + upVector;
    pc3->loc = loc + teeVector;

    float xp = Vec3::Cross_Product_Z(Vec3(upVector.X, upVector.Y, 0), Vec3(teeVector.X, teeVector.Y, 0));
    Vec2 rightTee = teeVector;
    if (xp < 0) { rightTee.X = -teeVector.X; rightTee.Y = -teeVector.Y; }
    rightTee *= widthInTexture;

    xp = Vec3::Cross_Product_Z(Vec3(upVector.X, upVector.Y, 0), Vec3(pc1->top.X - pc1->loc.X, pc1->top.Y - pc1->loc.Y, 0));
    if (xp > 0) { pc1->bottom = pc1->loc - rightTee; pc1->top = pc1->loc + rightTee; }
    else { pc1->bottom = pc1->loc + rightTee; pc1->top = pc1->loc - rightTee; }

    xp = Vec3::Cross_Product_Z(Vec3(upVector.X, upVector.Y, 0), Vec3(pc2->top.X - pc2->loc.X, pc2->top.Y - pc2->loc.Y, 0));
    if (xp > 0) { pc2->bottom = pc2->loc - rightTee; pc2->top = pc2->loc + rightTee; }
    else { pc2->bottom = pc2->loc + rightTee; pc2->top = pc2->loc - rightTee; }

    upVector *= widthInTexture;
    xp = Vec3::Cross_Product_Z(Vec3(rightTee.X, rightTee.Y, 0), Vec3(pc3->top.X - pc3->loc.X, pc3->top.Y - pc3->loc.Y, 0));
    if (xp < 0) { pc3->bottom = pc3->loc - upVector; pc3->top = pc3->loc + upVector; }
    else { pc3->bottom = pc3->loc + upVector; pc3->top = pc3->loc - upVector; }
}

// =========================================================================
// offsetH (line 2222)
// =========================================================================
void RoadBuilder::offsetH(TRoadPt* pc1, TRoadPt* pc2, TRoadPt* pc3,
                          Vec2 loc, Vec2 upVector, Vec2 teeVector,
                          bool flip, bool mirror, float widthInTexture)
{
    if (flip != mirror) {
        pc1->loc = loc - upVector * 2.05f;
        pc2->loc = loc + upVector * 0.46f;
    } else {
        pc1->loc = loc - upVector * 0.46f;
        pc2->loc = loc + upVector * 2.05f;
    }

    float xp = Vec3::Cross_Product_Z(Vec3(upVector.X, upVector.Y, 0), Vec3(teeVector.X, teeVector.Y, 0));
    Vec2 rightTee = teeVector;
    if (xp < 0) { rightTee.X = -teeVector.X; rightTee.Y = -teeVector.Y; }
    rightTee *= widthInTexture;

    xp = Vec3::Cross_Product_Z(Vec3(upVector.X, upVector.Y, 0), Vec3(pc1->top.X - pc1->loc.X, pc1->top.Y - pc1->loc.Y, 0));
    if (xp > 0) { pc1->bottom = pc1->loc - rightTee; pc1->top = pc1->loc + rightTee; }
    else { pc1->bottom = pc1->loc + rightTee; pc1->top = pc1->loc - rightTee; }

    xp = Vec3::Cross_Product_Z(Vec3(upVector.X, upVector.Y, 0), Vec3(pc2->top.X - pc2->loc.X, pc2->top.Y - pc2->loc.Y, 0));
    if (xp > 0) { pc2->bottom = pc2->loc - rightTee; pc2->top = pc2->loc + rightTee; }
    else { pc2->bottom = pc2->loc + rightTee; pc2->top = pc2->loc - rightTee; }

    Vec2 arm = teeVector;
    if (flip) { arm.Rotate(PI_F / 4); } else { arm.Rotate(-PI_F / 4); }
    Vec2 armNormal(-arm.Y, arm.X);
    armNormal *= widthInTexture;

    pc3->loc += arm * 2.10f;
    int xpSgn = xpSign(arm, pc3->top - loc);
    if (xpSgn == 1) { pc3->top = pc3->loc + armNormal; pc3->bottom = pc3->loc - armNormal; }
    else { pc3->top = pc3->loc - armNormal; pc3->bottom = pc3->loc + armNormal; }
}

// =========================================================================
// offsetY (line 2294)
// =========================================================================
void RoadBuilder::offsetY(TRoadPt* pc1, TRoadPt* pc2, TRoadPt* pc3,
                          Vec2 loc, Vec2 upVector, float widthInTexture)
{
    pc3->loc += upVector * 0.55f;
    pc3->top += upVector * 0.55f;
    pc3->bottom += upVector * 0.55f;

    Vec2 arm = upVector;
    arm.Rotate(3 * PI_F / 4);
    Vec2 armNormal(-arm.Y, arm.X);
    armNormal *= widthInTexture;
    pc2->loc += arm * 1.1f;
    int xp = xpSign(arm, pc2->top - loc);
    if (xp == 1) { pc2->top = pc2->loc + armNormal; pc2->bottom = pc2->loc - armNormal; }
    else { pc2->top = pc2->loc - armNormal; pc2->bottom = pc2->loc + armNormal; }

    arm = upVector;
    arm.Rotate(-3 * PI_F / 4);
    armNormal.Set(-arm.Y, arm.X);
    armNormal *= widthInTexture;

    pc1->loc += arm * 1.1f;
    xp = xpSign(arm, pc1->top - loc);
    if (xp == 1) { pc1->top = pc1->loc + armNormal; pc1->bottom = pc1->loc - armNormal; }
    else { pc1->top = pc1->loc - armNormal; pc1->bottom = pc1->loc + armNormal; }
}

// =========================================================================
// offset4Way (line 2340)
// =========================================================================
void RoadBuilder::offset4Way(TRoadPt* pc1, TRoadPt* pc2, TRoadPt* pc3, TRoadPt* pr3,
                             TRoadPt* pc4, Vec2 loc, Vec2 alignVector, float widthInTexture)
{
    pc1->loc = loc - alignVector;
    pc2->loc = loc + alignVector;

    Vec2 v3 = pr3->loc - loc;
    float angle_val = (PI_F / 2);
    float xp = Vec3::Cross_Product_Z(Vec3(alignVector.X, alignVector.Y, 0), Vec3(v3.X, v3.Y, 0));
    if (xp < 0) angle_val = -angle_val;
    Vec2 teeVector(alignVector);
    teeVector.Rotate(angle_val);
    pc3->loc = loc + teeVector;
    pc4->loc = loc - teeVector;

    Vec2 realTee(alignVector);
    realTee.Rotate(PI_F / 2);
    realTee *= widthInTexture;

    Vec3 align3(alignVector.X, alignVector.Y, 0);

    xp = Vec3::Cross_Product_Z(align3, Vec3(pc1->top.X - pc1->loc.X, pc1->top.Y - pc1->loc.Y, 0));
    if (xp > 0) { pc1->bottom = pc1->loc - realTee; pc1->top = pc1->loc + realTee; }
    else { pc1->bottom = pc1->loc + realTee; pc1->top = pc1->loc - realTee; }

    xp = Vec3::Cross_Product_Z(align3, Vec3(pc2->top.X - pc2->loc.X, pc2->top.Y - pc2->loc.Y, 0));
    if (xp > 0) { pc2->bottom = pc2->loc - realTee; pc2->top = pc2->loc + realTee; }
    else { pc2->bottom = pc2->loc + realTee; pc2->top = pc2->loc - realTee; }

    alignVector *= widthInTexture;
    Vec3 tee3(realTee.X, realTee.Y, 0);
    xp = Vec3::Cross_Product_Z(tee3, Vec3(pc3->top.X - pc3->loc.X, pc3->top.Y - pc3->loc.Y, 0));
    if (xp < 0) { pc3->bottom = pc3->loc - alignVector; pc3->top = pc3->loc + alignVector; }
    else { pc3->bottom = pc3->loc + alignVector; pc3->top = pc3->loc - alignVector; }

    xp = Vec3::Cross_Product_Z(tee3, Vec3(pc4->top.X - pc4->loc.X, pc4->top.Y - pc4->loc.Y, 0));
    if (xp < 0) { pc4->bottom = pc4->loc - alignVector; pc4->top = pc4->loc + alignVector; }
    else { pc4->bottom = pc4->loc + alignVector; pc4->top = pc4->loc - alignVector; }

    pc1->last = true; pc1->count = 0;
    pc2->last = true; pc2->count = 0;
    pc3->last = true; pc3->count = 0;
    pc4->last = true; pc4->count = 0;
}

// =========================================================================
// insert4Way (line 2414)
// =========================================================================
void RoadBuilder::insert4Way(Vec2 loc, int index1, float scale) {
    TRoadPt* pr1 = nullptr; TRoadPt* pr2 = nullptr; TRoadPt* pr3 = nullptr; TRoadPt* pr4 = nullptr;
    TRoadPt* pc1 = nullptr; TRoadPt* pc2 = nullptr; TRoadPt* pc3 = nullptr; TRoadPt* pc4 = nullptr;

    if (m_roads[index1].m_pt1.loc == loc) {
        pr1 = &m_roads[index1].m_pt2; pc1 = &m_roads[index1].m_pt1;
    } else {
        pr1 = &m_roads[index1].m_pt1; pc1 = &m_roads[index1].m_pt2;
    }
    int i;
    for (i = index1 + 1; i < m_numRoads; i++) {
        if (m_roads[i].m_pt1.loc == loc) {
            m_roads[i].m_pt1.count = -2;
            if (pr2 == nullptr) { pr2 = &m_roads[i].m_pt2; pc2 = &m_roads[i].m_pt1; }
            else if (pr3 == nullptr) { pr3 = &m_roads[i].m_pt2; pc3 = &m_roads[i].m_pt1; }
            else { pr4 = &m_roads[i].m_pt2; pc4 = &m_roads[i].m_pt1; }
        }
        if (m_roads[i].m_pt2.loc == loc) {
            m_roads[i].m_pt2.count = -2;
            if (pr2 == nullptr) { pr2 = &m_roads[i].m_pt1; pc2 = &m_roads[i].m_pt2; }
            else if (pr3 == nullptr) { pr3 = &m_roads[i].m_pt1; pc3 = &m_roads[i].m_pt2; }
            else { pr4 = &m_roads[i].m_pt1; pc4 = &m_roads[i].m_pt2; }
        }
    }
    if (pr2 == nullptr || pr3 == nullptr || pr4 == nullptr) return;

    Vec2 v1 = pr1->loc - loc; v1.Normalize();
    Vec2 v2 = pr2->loc - loc; v2.Normalize();
    Vec2 v3 = pr3->loc - loc; v3.Normalize();
    Vec2 v4 = pr4->loc - loc; v4.Normalize();

    float dot12 = Vec2::Dot_Product(v1, v2);
    float dot13 = Vec2::Dot_Product(v1, v3);
    float dot14 = Vec2::Dot_Product(v1, v4);
    float dot23 = Vec2::Dot_Product(v2, v3);
    float dot24 = Vec2::Dot_Product(v2, v4);
    float dot34 = Vec2::Dot_Product(v3, v4);

    int curPair = 12;
    float curDot = dot12;
    if (dot13 < curDot) { curPair = 13; curDot = dot13; }
    if (dot14 < curDot) { curPair = 14; curDot = dot14; }
    if (dot23 < curDot) { curPair = 23; curDot = dot23; }
    if (dot24 < curDot) { curPair = 24; curDot = dot24; }
    if (dot34 < curDot) { curPair = 34; curDot = dot34; }

    bool do12 = (curPair == 12);
    bool do13 = (curPair == 13);
    bool do14 = (curPair == 14);
    bool do23 = (curPair == 23);
    bool do24 = (curPair == 24);
    bool do34 = (curPair == 34);

    Vec2 alignVector;
    if (do12) alignVector = v2 - v1;
    if (do13) alignVector = v3 - v1;
    if (do14) alignVector = v4 - v1;
    if (do23) alignVector = v3 - v2;
    if (do24) alignVector = v4 - v2;
    if (do34) alignVector = v4 - v3;
    alignVector.Normalize();
    alignVector *= 0.5f * scale;

    if (do12) offset4Way(pc1, pc2, pc3, pr3, pc4, loc, alignVector, m_roads[index1].m_widthInTexture);
    if (do13) offset4Way(pc1, pc3, pc2, pr2, pc4, loc, alignVector, m_roads[index1].m_widthInTexture);
    if (do14) offset4Way(pc1, pc4, pc3, pr3, pc2, loc, alignVector, m_roads[index1].m_widthInTexture);
    if (do23) offset4Way(pc2, pc3, pc1, pr1, pc4, loc, alignVector, m_roads[index1].m_widthInTexture);
    if (do24) offset4Way(pc2, pc4, pc1, pr1, pc3, loc, alignVector, m_roads[index1].m_widthInTexture);
    if (do34) offset4Way(pc3, pc4, pc1, pr1, pc2, loc, alignVector, m_roads[index1].m_widthInTexture);

    if (alignVector.X < 0) {
        alignVector.X = -alignVector.X;
        alignVector.Y = -alignVector.Y;
    }
    CHECK_SEGMENTS;
    m_roads[m_numRoads].m_pt1.loc.Set(loc);
    m_roads[m_numRoads].m_pt2.loc.Set(loc + alignVector);
    m_roads[m_numRoads].m_pt1.last = true;
    m_roads[m_numRoads].m_pt2.last = true;
    m_roads[m_numRoads].m_scale = m_roads[index1].m_scale;
    m_roads[m_numRoads].m_widthInTexture = TEE_WIDTH_ADJUSTMENT;
    m_roads[m_numRoads].m_pt1.count = -4;
    m_roads[m_numRoads].m_type = FOUR_WAY;
    m_roads[m_numRoads].m_uniqueID = m_roads[index1].m_uniqueID;
    m_numRoads++;
}

// =========================================================================
// insertTeeIntersections (line 2584)
// =========================================================================
void RoadBuilder::insertTeeIntersections() {
    int numRoadSegments = m_numRoads;
    int i;
    for (i = 0; i < numRoadSegments; i++) {
        if (m_roads[i].m_type != SEGMENT) continue;
        if (m_roads[i].m_pt1.count == 2) {
            insertTee(m_roads[i].m_pt1.loc, i, m_roads[i].m_scale);
        }
        if (m_roads[i].m_pt2.count == 2) {
            insertTee(m_roads[i].m_pt2.loc, i, m_roads[i].m_scale);
        }
        if (m_roads[i].m_pt1.count == 3) {
            insert4Way(m_roads[i].m_pt1.loc, i, m_roads[i].m_scale);
        }
        if (m_roads[i].m_pt2.count == 3) {
            insert4Way(m_roads[i].m_pt2.loc, i, m_roads[i].m_scale);
        }
    }
}

// =========================================================================
// findCrossTypeJoinVector (line ~2645)
// =========================================================================
int RoadBuilder::findCrossTypeJoinVector(Vec2 loc, Vec2* joinVector, int uniqueID) {
    Vec2 newVector = *joinVector;
    int numRoadSegments = m_numRoads;
    int i;
    for (i = 0; i < numRoadSegments; i++) {
        if (m_roads[i].m_uniqueID == uniqueID) continue;
        if (m_roads[i].m_type != SEGMENT) continue;
        Vec2 loc1 = m_roads[i].m_pt1.loc;
        Vec2 loc2 = m_roads[i].m_pt2.loc;

        // Bounding box check
        float loX = loc1.X, loY = loc1.Y, hiX = loc1.X, hiY = loc1.Y;
        if (loc2.X < loX) loX = loc2.X;
        if (loc2.Y < loY) loY = loc2.Y;
        if (loc2.X > hiX) hiX = loc2.X;
        if (loc2.Y > hiY) hiY = loc2.Y;
        float halfScale = m_roads[i].m_scale / 2;
        loX -= halfScale; loY -= halfScale;
        hiX += halfScale; hiY += halfScale;
        if (loc.X >= loX && loc.Y >= loY && loc.X <= hiX && loc.Y <= hiY) {
            Vec3 v1(loc1.X, loc1.Y, 0);
            Vec3 v2(loc2.X, loc2.Y, 0);
            LineSeg roadLine(v1, v2);
            Vec3 vLoc(loc.X, loc.Y, 0);
            Vec3 ptOnLine = roadLine.Find_Point_Closest_To(vLoc);
            float dist = Vec3::Distance(ptOnLine, vLoc);
            if (dist < m_roads[i].m_scale * 0.55f) {
                Vec2 roadVec = loc2 - loc1;
                if (xpSign(roadVec, *joinVector) == 1) {
                    roadVec.Rotate(PI_F / 2);
                } else {
                    roadVec.Rotate(-PI_F / 2);
                }
                newVector = roadVec;
                *joinVector = newVector;
                return m_roads[i].m_uniqueID;
            }
        }
    }
    return 0;
}

// =========================================================================
// adjustStacking (line 2699)
// =========================================================================
void RoadBuilder::adjustStacking(int topUniqueID, int bottomUniqueID) {
    int i, j;
    for (i = 0; i < m_maxRoadTypes; i++) {
        if (m_roadTypes[i].uniqueID == topUniqueID) break;
    }
    if (i >= m_maxRoadTypes) return;

    for (j = 0; j < m_maxRoadTypes; j++) {
        if (m_roadTypes[j].uniqueID == bottomUniqueID) break;
    }
    if (j >= m_maxRoadTypes) return;

    if (m_roadTypes[i].stackingOrder > m_roadTypes[j].stackingOrder) {
        return; // already on top
    }
    int newStacking = m_roadTypes[j].stackingOrder;
    for (j = 0; j < m_maxRoadTypes; j++) {
        if (m_roadTypes[j].stackingOrder > newStacking) {
            m_roadTypes[j].stackingOrder = m_roadTypes[j].stackingOrder + 1;
        }
    }
    m_roadTypes[i].stackingOrder = newStacking + 1;
}

// =========================================================================
// insertCrossTypeJoins (line 2733)
// =========================================================================
void RoadBuilder::insertCrossTypeJoins() {
    int numRoadSegments = m_numRoads;
    int i;
    for (i = 0; i < numRoadSegments; i++) {
        Vec2 loc1, loc2;
        bool isPt1 = false;
        if ((m_roads[i].m_pt2.count == 0 && m_roads[i].m_pt2.isJoin)) {
            loc1 = m_roads[i].m_pt2.loc;
            loc2 = m_roads[i].m_pt1.loc;
        } else if ((m_roads[i].m_pt1.count == 0 && m_roads[i].m_pt1.isJoin)) {
            loc1 = m_roads[i].m_pt1.loc;
            loc2 = m_roads[i].m_pt2.loc;
            isPt1 = true;
        } else {
            continue;
        }
        Vec2 joinVector(1, 0);
        Vec2 roadVector(loc2.X - loc1.X, loc2.Y - loc1.Y);
        roadVector.Normalize();
        joinVector = roadVector;
        int otherID = findCrossTypeJoinVector(loc1, &joinVector, m_roads[i].m_uniqueID);
        if (!otherID) {
            joinVector *= 100;
        }
        Vec2 roadNormal(-roadVector.Y, roadVector.X);
        Vec2 joinNormal(-joinVector.Y, joinVector.X);

        Vec2 p1 = loc1 + roadNormal * m_roads[i].m_scale * m_roads[i].m_widthInTexture / 2;
        Vec2 p2 = loc2 + roadNormal * m_roads[i].m_scale * m_roads[i].m_widthInTexture / 2;

        Vec3 v1(p1.X, p1.Y, 0);
        Vec3 v2(p2.X, p2.Y, 0);
        LineSeg roadLine(v1, v2);
        Vec3 vLoc1(loc1.X, loc1.Y, 0);
        v1 = Vec3(joinNormal.X, joinNormal.Y, 0) + vLoc1;
        LineSeg joinLine(vLoc1, v1);
        Vec3 pInt1, pInt2;

        float nu;
        Vec2 top = m_roads[i].m_pt1.top;
        if (joinLine.Find_Intersection(roadLine, &pInt1, &nu, &pInt2, &nu)) {
            if (isPt1) {
                m_roads[i].m_pt1.top.Set(pInt1.X, pInt1.Y);
                top = m_roads[i].m_pt1.top;
            } else {
                m_roads[i].m_pt2.bottom.Set(pInt1.X, pInt1.Y);
                top = m_roads[i].m_pt2.bottom;
            }
        }
        p1 = loc1 - roadNormal * m_roads[i].m_scale * m_roads[i].m_widthInTexture / 2;
        p2 = loc2 - roadNormal * m_roads[i].m_scale * m_roads[i].m_widthInTexture / 2;
        v1.Set(p1.X, p1.Y, 0);
        v2.Set(p2.X, p2.Y, 0);
        roadLine.Set(v1, v2);
        Vec2 bottom = m_roads[i].m_pt1.bottom;
        if (joinLine.Find_Intersection(roadLine, &pInt1, &nu, &pInt2, &nu)) {
            if (isPt1) {
                m_roads[i].m_pt1.bottom.Set(pInt1.X, pInt1.Y);
                bottom = m_roads[i].m_pt1.bottom;
            } else {
                m_roads[i].m_pt2.top.Set(pInt1.X, pInt1.Y);
                bottom = m_roads[i].m_pt2.top;
            }
        }

        bottom = bottom - top;
        float scaleAdjustment = bottom.Length() / (m_roads[i].m_scale * m_roads[i].m_widthInTexture);
        if (otherID) {
            adjustStacking(m_roads[i].m_uniqueID, otherID);
        }
        CHECK_SEGMENTS;
        m_roads[m_numRoads].m_pt1.loc.Set(loc1);
        m_roads[m_numRoads].m_pt2.loc.Set(loc1 + joinVector);
        m_roads[m_numRoads].m_pt1.last = true;
        m_roads[m_numRoads].m_pt2.last = true;
        m_roads[m_numRoads].m_scale = m_roads[i].m_scale;
        m_roads[m_numRoads].m_widthInTexture = m_roads[i].m_scale * scaleAdjustment;
        m_roads[m_numRoads].m_pt1.count = 0;
        m_roads[m_numRoads].m_type = ALPHA_JOIN;
        m_roads[m_numRoads].m_uniqueID = m_roads[i].m_uniqueID;
        m_numRoads++;
    }
}

// =========================================================================
// loadTee (line 363)
// =========================================================================
void RoadBuilder::loadTee(RoadSegment* pRoad, Vec2 loc1, Vec2 loc2, bool is4Way, float scale) {
    Vec2 roadVector(loc2.X - loc1.X, loc2.Y - loc1.Y);
    Vec2 roadNormal(-roadVector.Y, roadVector.X);
    if (fabsf(roadVector.X) < MIN_ROAD_SEGMENT && fabsf(roadVector.Y) < MIN_ROAD_SEGMENT) {
        roadVector.Set(1.0f, 0.0f);
        roadNormal.Set(0.0f, 1.0f);
    } else {
        roadVector.Normalize();
        roadNormal.Normalize();
    }

    float uOffset = (425.0f / 512.0f);
    float vOffset = (255.0f / 512.0f);
    if (is4Way) {
        uOffset = (425.0f / 512.0f);
        vOffset = (425.0f / 512.0f);
    }
    float teeFactor = scale * TEE_WIDTH_ADJUSTMENT / 2.0f;
    float left = pRoad->m_widthInTexture * scale / 2.0f;
    loadFloatSection(pRoad, loc1, loc2 - loc1, teeFactor, left, teeFactor, uOffset, vOffset, scale);
}

// =========================================================================
// loadAlphaJoin (line 393)
// =========================================================================
void RoadBuilder::loadAlphaJoin(RoadSegment* pRoad, Vec2 loc1, Vec2 loc2, float scale) {
    Vec2 roadVector(loc2.X - loc1.X, loc2.Y - loc1.Y);
    Vec2 roadNormal(-roadVector.Y, roadVector.X);
    if (fabsf(roadVector.X) < MIN_ROAD_SEGMENT && fabsf(roadVector.Y) < MIN_ROAD_SEGMENT) {
        roadVector.Set(1.0f, 0.0f);
        roadNormal.Set(0.0f, 1.0f);
    } else {
        roadVector.Normalize();
        roadNormal.Normalize();
    }

    float uOffset = (106.0f / 512.0f);
    float vOffset = (425.0f / 512.0f);

    float roadwidth = scale;
    float uScale = pRoad->m_widthInTexture;

    roadVector *= roadwidth * 48 / 128;
    roadNormal *= uScale * (1 + 8.0f / 128);

    Vec2 corners[NUM_CORNERS];
    corners[topLeft] = loc1 + roadNormal * 0.5f - roadVector * 0.65f;
    corners[bottomLeft] = corners[topLeft] - roadNormal;
    corners[bottomRight] = corners[bottomLeft] + roadVector;
    corners[topRight] = corners[topLeft] + roadVector;
    loadFloat4PtSection(pRoad, loc1, roadNormal, roadVector, corners, uOffset, vOffset, scale, uScale);
}

// =========================================================================
// loadY (line 432)
// =========================================================================
void RoadBuilder::loadY(RoadSegment* pRoad, Vec2 loc1, Vec2 loc2, float scale) {
    Vec2 roadVector(loc2.X - loc1.X, loc2.Y - loc1.Y);
    Vec2 roadNormal(-roadVector.Y, roadVector.X);
    if (fabsf(roadVector.X) < MIN_ROAD_SEGMENT && fabsf(roadVector.Y) < MIN_ROAD_SEGMENT) {
        roadVector.Set(1.0f, 0.0f);
        roadNormal.Set(0.0f, 1.0f);
    } else {
        roadVector.Normalize();
        roadNormal.Normalize();
    }

    float uOffset = (255.0f / 512.0f);
    float vOffset = (226.0f / 512.0f);

    float roadwidth = scale;
    roadVector *= roadwidth;
    roadNormal *= roadwidth;

    Vec2 corners[NUM_CORNERS];
    roadVector *= 1.59f;
    corners[topLeft] = loc1 + roadNormal * 0.29f - roadVector * 0.5f;
    corners[bottomLeft] = corners[topLeft] - roadNormal * 1.08f;
    corners[bottomRight] = corners[bottomLeft] + roadVector;
    corners[topRight] = corners[topLeft] + roadVector;
    loadFloat4PtSection(pRoad, loc1, roadNormal, roadVector, corners, uOffset, vOffset, scale, scale);
}

// =========================================================================
// loadH (line 471)
// =========================================================================
void RoadBuilder::loadH(RoadSegment* pRoad, Vec2 loc1, Vec2 loc2, bool flip, float scale) {
    Vec2 roadVector(loc2.X - loc1.X, loc2.Y - loc1.Y);
    Vec2 roadNormal(-roadVector.Y, roadVector.X);
    if (fabsf(roadVector.X) < MIN_ROAD_SEGMENT && fabsf(roadVector.Y) < MIN_ROAD_SEGMENT) {
        roadVector.Set(1.0f, 0.0f);
        roadNormal.Set(0.0f, 1.0f);
    } else {
        roadVector.Normalize();
        roadNormal.Normalize();
    }

    float uOffset = (202.0f / 512.0f);
    float vOffset = (364.0f / 512.0f);

    float roadwidth = scale;
    roadVector *= roadwidth;
    roadNormal *= roadwidth;

    Vec2 corners[NUM_CORNERS];
    roadNormal *= 1.35f;
    if (flip) {
        corners[bottomLeft] = loc1 - roadNormal * 0.20f - roadVector * pRoad->m_widthInTexture / 2;
    } else {
        corners[bottomLeft] = loc1 - roadNormal * 0.8f - roadVector * pRoad->m_widthInTexture / 2;
    }
    Vec2 width = roadVector * pRoad->m_widthInTexture / 2;
    width = width + roadVector * 1.2f;
    corners[bottomRight] = corners[bottomLeft] + width;
    corners[topRight] = corners[bottomRight] + roadNormal;
    corners[topLeft] = corners[bottomLeft] + roadNormal;
    if (flip) roadNormal = -roadNormal;
    loadFloat4PtSection(pRoad, loc1, roadNormal, roadVector, corners, uOffset, vOffset, scale, scale);
}

// =========================================================================
// loadFloatSection (line 518)
// =========================================================================
void RoadBuilder::loadFloatSection(RoadSegment* pRoad, Vec2 loc,
    Vec2 roadVector, float halfHeight, float left, float right,
    float uOffset, float vOffset, float scale)
{
    if (!m_heightMap) return;

    Vec2 roadNormal(-roadVector.Y, roadVector.X);
    roadVector.Normalize();
    roadVector *= right;

    roadNormal.Normalize();
    if (halfHeight < 0) halfHeight = -halfHeight;
    roadNormal *= halfHeight;

    Vec2 roadLeft = roadVector;
    roadLeft.Normalize();
    roadLeft *= left;
    roadVector += roadLeft;
    Vec2 leftCenter = loc;
    leftCenter -= roadLeft;

    Vec2 corners[NUM_CORNERS];
    corners[bottomLeft] = leftCenter - roadNormal;
    corners[bottomRight] = corners[bottomLeft];
    corners[bottomRight] += roadVector;
    corners[topRight] = corners[bottomRight];
    corners[topRight] += 2 * roadNormal;
    corners[topLeft] = corners[bottomLeft];
    corners[topLeft] += 2 * roadNormal;

    loadFloat4PtSection(pRoad, loc, roadNormal, roadVector, corners, uOffset, vOffset, scale, scale);
}

// =========================================================================
// loadFloat4PtSection (line 564) -- THE CORE TESSELLATION FUNCTION
// =========================================================================
void RoadBuilder::loadFloat4PtSection(RoadSegment* pRoad, Vec2 loc,
    Vec2 roadNormal, Vec2 roadVector,
    Vec2* cornersP,
    float uOffset, float vOffset, float uScale, float vScale)
{
    const float FLOAT_AMOUNT = ROAD_HEIGHT_SCALE / 8;
    const float MAX_ERROR = ROAD_HEIGHT_SCALE * 1.1f;
    uint16_t ib[MAX_SEG_INDEX];
    RoadVertex vb[MAX_SEG_VERTEX];
    int numRoadVertices = 0;
    int numRoadIndices = 0;

    TRoadSegInfo info;
    info.loc = loc;
    info.roadNormal = roadNormal;
    info.roadVector = roadVector;
    info.corners[bottomLeft] = cornersP[bottomLeft];
    info.corners[bottomRight] = cornersP[bottomRight];
    info.corners[topLeft] = cornersP[topLeft];
    info.corners[topRight] = cornersP[topRight];
    info.uOffset = uOffset;
    info.vOffset = vOffset;
    info.scale = uScale;
    pRoad->SetRoadSegInfo(&info);

    float roadLen = roadVector.Length();
    float halfHeight = roadNormal.Length();
    roadNormal.Normalize();
    roadVector.Normalize();
    Vec2 curVector;
    int uCount = (int)(roadLen / ROAD_MAP_XY_FACTOR) + 1;
    if (uCount < 2) uCount = 2;
    int vCount = (int)(2 * halfHeight / ROAD_MAP_XY_FACTOR) + 1;
    if (vCount < 2) vCount = 2;

    const int maxRows = 100;
    struct TColumn {
        bool collapsed;
        bool deleted;
        Vec3 vtx[100]; // maxRows
        int diffuseRed;
        bool lightGradient;
        int vertexIndex[100]; // maxRows
        float uIndex;
    };
    const int DIFFUSE_LIMIT = 25;

    if (vCount > maxRows) vCount = maxRows;
    TColumn prevColumn, curColumn, nextColumn;

    prevColumn.deleted = true;
    curColumn.deleted = true;
    int i, j, k;
    Vec2 v2 = cornersP[bottomLeft];
    Vec3 origin(v2.X, v2.Y, 0);
    v2 = cornersP[bottomRight] - cornersP[bottomLeft];
    Vec3 uVector1(v2.X, v2.Y, 0);
    v2 = cornersP[topRight] - cornersP[topLeft];
    Vec3 uVector2(v2.X, v2.Y, 0);
    v2 = cornersP[topLeft];
    Vec3 origin2(v2.X, v2.Y, 0);
    v2 = cornersP[topLeft] - cornersP[bottomLeft];
    Vec3 vVector1(v2.X, v2.Y, 0);
    v2 = cornersP[topRight] - cornersP[bottomRight];
    Vec3 vVector2(v2.X, v2.Y, 0);
    uVector2 += (vVector1 - vVector2);
    for (i = 0; i <= uCount; i++) {
        float iFactor = ((float)i / (uCount - 1));
        float iBarFactor = 1.0f - iFactor;
        if (i < uCount) {
            nextColumn.collapsed = false;
            nextColumn.deleted = false;
            nextColumn.lightGradient = false;
            nextColumn.uIndex = (float)i;

            float minHeight = m_heightMap->getMaxHeightValue() * ROAD_HEIGHT_SCALE;
            float maxHeight = m_heightMap->getMinHeightValue() * ROAD_HEIGHT_SCALE;
            for (j = 0; j < vCount; j++) {
                float jFactor = ((float)j / (vCount - 1));
                float jBarFactor = 1.0f - jFactor;
                nextColumn.vtx[j] = origin + (uVector1 * jBarFactor * iFactor) + (uVector2 * jFactor * iFactor) +
                    (vVector1 * iBarFactor * jFactor) + (vVector2 * iFactor * jFactor);
                float z = m_heightMap->getMaxCellHeight(nextColumn.vtx[j].X, nextColumn.vtx[j].Y);
                if (z < minHeight) minHeight = z;
                if (z > maxHeight) maxHeight = z;
                nextColumn.vertexIndex[j] = -1;
                nextColumn.vtx[j].Z = z;
                int diffuse = 0;
                if (j == 0) {
                    nextColumn.diffuseRed = (diffuse & 0x00ff);
                } else {
                    int red = diffuse & 0x00ff;
                    if (abs(red - nextColumn.diffuseRed) > DIFFUSE_LIMIT) {
                        nextColumn.lightGradient = true;
                    }
                }
            }

            if (true) { // !nextColumn.lightGradient) -- original always takes this branch
                nextColumn.collapsed = true;
                nextColumn.vtx[0].Z = maxHeight;
                nextColumn.vtx[1] = nextColumn.vtx[vCount - 1];
                nextColumn.vtx[1].Z = maxHeight;
            } else {
                for (j = 0; j < vCount; j++) {
                    nextColumn.vtx[j].Z = maxHeight;
                }
            }
            if (i < 2) {
                curColumn = nextColumn;
            } else {
                if (prevColumn.collapsed && curColumn.collapsed && nextColumn.collapsed) {
                    bool okToDelete = false;
                    float theZ = prevColumn.vtx[0].Z * (curColumn.uIndex - prevColumn.uIndex) +
                        nextColumn.vtx[0].Z * (nextColumn.uIndex - curColumn.uIndex);
                    theZ /= nextColumn.uIndex - prevColumn.uIndex;
                    if (theZ >= curColumn.vtx[0].Z && theZ < curColumn.vtx[0].Z + MAX_ERROR) {
                        theZ = prevColumn.vtx[1].Z * (curColumn.uIndex - prevColumn.uIndex) +
                            nextColumn.vtx[1].Z * (nextColumn.uIndex - curColumn.uIndex);
                        theZ /= nextColumn.uIndex - prevColumn.uIndex;
                        if (theZ >= curColumn.vtx[1].Z && theZ < curColumn.vtx[1].Z + MAX_ERROR) {
                            okToDelete = true;
                        }
                    }
                    if (okToDelete) {
                        curColumn.deleted = true;
                    }
                }
            }
        }
        if (!curColumn.deleted && i != 1) {
            // Write out the vertices.
            for (j = 0; j < vCount; j++) {
                float U, V;
                if (numRoadVertices >= MAX_SEG_INDEX) {
                    break;
                }
                curVector.Set(curColumn.vtx[j].X - loc.X, curColumn.vtx[j].Y - loc.Y);
                V = Vec2::Dot_Product(roadNormal, curVector);
                U = Vec2::Dot_Product(roadVector, curVector);
                int diffuse = 0;
                vb[numRoadVertices].u = uOffset + U / (uScale * 4);
                vb[numRoadVertices].v = vOffset - V / (vScale * 4);
                vb[numRoadVertices].x = curColumn.vtx[j].X;
                vb[numRoadVertices].y = curColumn.vtx[j].Y;
                vb[numRoadVertices].z = curColumn.vtx[j].Z + FLOAT_AMOUNT;
                vb[numRoadVertices].diffuse = diffuse;
                curColumn.vertexIndex[j] = numRoadVertices;
                numRoadVertices++;
                if (j == 1 && curColumn.collapsed) {
                    break;
                }
            }
            if (numRoadVertices >= MAX_SEG_INDEX) {
                break;
            }
            if (i > 1) {
                // Write out the triangles.
                j = 0;
                k = 0;
                while (j < vCount - 1 && k < vCount - 1) {
                    if (numRoadIndices >= MAX_SEG_INDEX) {
                        break;
                    }
                    uint16_t* curIb = ib + numRoadIndices;
                    if (k == 0 || !prevColumn.collapsed) {
                        *curIb++ = (uint16_t)prevColumn.vertexIndex[j + 1];
                        *curIb++ = (uint16_t)prevColumn.vertexIndex[j];
                        *curIb++ = (uint16_t)curColumn.vertexIndex[k];
                        numRoadIndices += 3;
                    }
                    if (j == 0 || !curColumn.collapsed) {
                        int offset = 1;
                        if (curColumn.collapsed && !prevColumn.collapsed) {
                            offset = vCount - 1;
                        }
                        *curIb++ = (uint16_t)prevColumn.vertexIndex[j + offset];
                        *curIb++ = (uint16_t)curColumn.vertexIndex[k];
                        *curIb++ = (uint16_t)curColumn.vertexIndex[k + 1];
                        numRoadIndices += 3;
                    }
                    if (prevColumn.collapsed && curColumn.collapsed) {
                        break;
                    }
                    if (!prevColumn.collapsed) {
                        j++;
                    }
                    if (!curColumn.collapsed) {
                        k++;
                    }
                }
                prevColumn = curColumn;
            } else if (i == 0) {
                prevColumn = curColumn;
            }
            if (numRoadIndices >= MAX_SEG_INDEX) {
                break;
            }
        }
        curColumn = nextColumn;
    }
    pRoad->SetVertexBuffer(vb, numRoadVertices);
    pRoad->SetIndexBuffer(ib, numRoadIndices);
}

// =========================================================================
// loadCurve (line 1062)
// =========================================================================
void RoadBuilder::loadCurve(RoadSegment* pRoad, Vec2 loc1, Vec2 loc2, float scale) {
    float uOffset = (4.0f / 512.0f);
    float vOffset = (255.0f / 512.0f);
    if (pRoad->m_curveRadius == TIGHT_CORNER_RADIUS) {
        vOffset = (425.0f / 512.0f);
    }

    Vec2 roadVector(loc2.X - loc1.X, loc2.Y - loc1.Y);
    Vec2 roadNormal(-roadVector.Y, roadVector.X);
    float roadLen = scale;

    float curveHeight = pRoad->m_widthInTexture * scale / 2.0f;

    roadVector.Normalize();
    roadVector *= roadLen;

    roadNormal.Normalize();
    roadNormal *= fabsf(curveHeight);

    Vec2 corners[NUM_CORNERS];

    if (pRoad->m_curveRadius == TIGHT_CORNER_RADIUS) {
        corners[bottomLeft] = loc1 - roadNormal;
        corners[bottomRight] = corners[bottomLeft];
        corners[bottomRight] += roadVector * 0.5f;
        corners[topRight] = corners[bottomRight];
        corners[topRight] += 2 * roadNormal;
        corners[topLeft] = corners[bottomLeft];
        corners[topLeft] += 2 * roadNormal;

        corners[bottomRight] += roadVector * 0.1f;
        corners[bottomRight] += roadNormal * 0.2f;
        corners[bottomLeft] -= roadNormal * 0.1f;
        corners[bottomLeft] -= roadVector * 0.02f;
        corners[topLeft] -= roadVector * 0.02f;

        corners[topRight] -= roadVector * 0.4f;
        corners[topRight] += roadNormal * 0.2f;
    } else {
        corners[bottomLeft] = loc1 - roadNormal;
        corners[bottomRight] = corners[bottomLeft];
        corners[bottomRight] += roadVector;
        corners[topRight] = corners[bottomRight];
        corners[topRight] += 2 * roadNormal;
        corners[topLeft] = corners[bottomLeft];
        corners[topLeft] += 2 * roadNormal;

        corners[bottomRight] += roadVector * 0.1f;
        corners[bottomRight] += roadNormal * 0.4f;
        corners[bottomLeft] -= roadNormal * 0.2f;
        corners[bottomLeft] -= roadVector * 0.02f;
        corners[topLeft] -= roadVector * 0.02f;

        corners[topRight] -= roadVector * 0.4f;
        corners[topRight] += roadNormal * 0.4f;
    }

    loadFloat4PtSection(pRoad, loc1, roadNormal, roadVector, corners, uOffset, vOffset, scale, scale);
}

// =========================================================================
// preloadRoadSegment (line 1139)
// =========================================================================
void RoadBuilder::preloadRoadSegment(RoadSegment* pRoad) {
    Vec2 roadVector = pRoad->m_pt2.loc - pRoad->m_pt1.loc;
    Vec2 roadNormal(-roadVector.Y, roadVector.X);

    float uOffset = 0.0f;
    float vOffset = (85.0f / 512.0f);

    float roadHeight = pRoad->m_widthInTexture * pRoad->m_scale / 2.0f;
    roadNormal.Normalize();
    roadNormal *= roadHeight;

    Vec2 corners[NUM_CORNERS];
    corners[bottomLeft] = pRoad->m_pt1.bottom;
    corners[topLeft] = pRoad->m_pt1.top;
    corners[bottomRight] = pRoad->m_pt2.bottom;
    corners[topRight] = pRoad->m_pt2.top;
    loadFloat4PtSection(pRoad, pRoad->m_pt1.loc, roadNormal, roadVector, corners, uOffset, vOffset,
        pRoad->m_scale, pRoad->m_scale);
}

// =========================================================================
// preloadRoadsInVertexAndIndexBuffers (line 1168)
// =========================================================================
void RoadBuilder::preloadRoadsInVertexAndIndexBuffers() {
    int curRoad;

    // Do road segments.
    for (curRoad = 0; curRoad < m_numRoads; curRoad++) {
        if (m_roads[curRoad].m_type == SEGMENT) {
            preloadRoadSegment(&m_roads[curRoad]);
        }
    }
    // Do curves.
    for (curRoad = 0; curRoad < m_numRoads; curRoad++) {
        if (m_roads[curRoad].m_type == CURVE) {
            loadCurve(&m_roads[curRoad], m_roads[curRoad].m_pt1.loc, m_roads[curRoad].m_pt2.loc, m_roads[curRoad].m_scale);
        }
    }
    // Do Y tees.
    for (curRoad = 0; curRoad < m_numRoads; curRoad++) {
        if (m_roads[curRoad].m_type == THREE_WAY_Y) {
            loadY(&m_roads[curRoad], m_roads[curRoad].m_pt1.loc, m_roads[curRoad].m_pt2.loc,
                m_roads[curRoad].m_scale);
        }
    }
    // Do H tees.
    for (curRoad = 0; curRoad < m_numRoads; curRoad++) {
        if (m_roads[curRoad].m_type == THREE_WAY_H || m_roads[curRoad].m_type == THREE_WAY_H_FLIP) {
            loadH(&m_roads[curRoad], m_roads[curRoad].m_pt1.loc, m_roads[curRoad].m_pt2.loc,
                m_roads[curRoad].m_type == THREE_WAY_H_FLIP, m_roads[curRoad].m_scale);
        }
    }
    // Do standard tees and 4-ways.
    for (curRoad = 0; curRoad < m_numRoads; curRoad++) {
        if (m_roads[curRoad].m_type == TEE || m_roads[curRoad].m_type == FOUR_WAY) {
            loadTee(&m_roads[curRoad], m_roads[curRoad].m_pt1.loc, m_roads[curRoad].m_pt2.loc,
                (m_roads[curRoad].m_type == FOUR_WAY), m_roads[curRoad].m_scale);
        }
    }
    // Do alpha joins.
    for (curRoad = 0; curRoad < m_numRoads; curRoad++) {
        if (m_roads[curRoad].m_type == ALPHA_JOIN) {
            loadAlphaJoin(&m_roads[curRoad], m_roads[curRoad].m_pt1.loc, m_roads[curRoad].m_pt2.loc,
                m_roads[curRoad].m_scale);
        }
    }

    // Set full white diffuse (shader handles lighting in D3D11 path)
    for (curRoad = 0; curRoad < m_numRoads; curRoad++) {
        if (m_roads[curRoad].m_numVertex > 0 && m_roads[curRoad].m_vb) {
            for (int v = 0; v < m_roads[curRoad].m_numVertex; v++) {
                m_roads[curRoad].m_vb[v].diffuse = 0xFFFFFFFF;
            }
        }
    }
}

// =========================================================================
// gatherBatches -- collects all road segment geometry into output batches
// =========================================================================
RoadMeshOutput RoadBuilder::gatherBatches() {
    RoadMeshOutput output;

    // Determine max stacking order
    int maxStacking = 0;
    for (int i = 0; i < m_maxRoadTypes; i++) {
        if (m_roadTypes[i].stackingOrder > maxStacking) {
            maxStacking = m_roadTypes[i].stackingOrder;
        }
    }

    // Walk stacking order, same as drawRoads
    for (int stacking = 0; stacking <= maxStacking; stacking++) {
        for (int rt = 0; rt < m_maxRoadTypes; rt++) {
            if (m_roadTypes[rt].stackingOrder != stacking) continue;
            int curUniqueID = m_roadTypes[rt].uniqueID;
            if (curUniqueID < 0) continue;

            RoadBatch batch;
            batch.roadTypeID = curUniqueID;
            batch.textureName = m_roadTypes[rt].textureName;

            (void)0;

            // Walk all corner types in order, same as loadRoadsInVertexAndIndexBuffers
            for (int corner = SEGMENT; corner < NUM_JOINS; corner++) {
                for (int curRoad = 0; curRoad < m_numRoads; curRoad++) {
                    if (m_roads[curRoad].m_type != corner) continue;
                    if (m_roads[curRoad].m_uniqueID != curUniqueID) continue;
                    int nv = m_roads[curRoad].GetNumVertex();
                    int ni = m_roads[curRoad].GetNumIndex();
                    if (nv <= 0 || ni <= 0) continue;

                    int baseVertex = (int)batch.vertices.size();
                    batch.vertices.resize(baseVertex + nv);
                    m_roads[curRoad].GetVertices(&batch.vertices[baseVertex], nv);

                    int baseIndex = (int)batch.indices.size();
                    batch.indices.resize(baseIndex + ni);
                    m_roads[curRoad].GetIndices(&batch.indices[baseIndex], ni, baseVertex);
                }
            }

            if (!batch.indices.empty()) {
                output.batches.push_back(std::move(batch));
            }
        }
    }

    return output;
}

// =========================================================================
// build -- runs the full pipeline
// =========================================================================
RoadMeshOutput RoadBuilder::build() {
    addMapObjects();
    updateCountsAndFlags();
    insertTeeIntersections();
    insertCurveSegments();
    insertCrossTypeJoins();
    preloadRoadsInVertexAndIndexBuffers();
    return gatherBatches();
}

// =========================================================================
// Public entry point
// =========================================================================
RoadMeshOutput GenerateRoadGeometry(WorldHeightMap* heightMap) {
    WorldHeightMapAdapter adapter(heightMap);
    RoadBuilder builder(&adapter);
    return builder.build();
}

} // namespace RoadGeometry
