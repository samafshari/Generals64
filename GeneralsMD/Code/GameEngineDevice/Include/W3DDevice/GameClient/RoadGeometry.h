// RoadGeometry.h
// Self-contained road geometry generation ported from W3DRoadBuffer.
// Zero DX8/WW3D dependencies -- pure geometry + height queries.
// Original author: John Ahlquist, May 2001. Ported April 2026.

#pragma once

#include <cstdint>
#include <cmath>
#include <vector>
#include <string>

class WorldHeightMap;
class MapObject;
class TerrainRoadType;

namespace RoadGeometry {

// ---- Constants (preserved from original) ----
static constexpr float ROAD_MAP_XY_FACTOR  = 10.0f;
static constexpr float ROAD_HEIGHT_SCALE   = ROAD_MAP_XY_FACTOR / 16.0f;
static constexpr float CORNER_RADIUS       = 1.5f;
static constexpr float TIGHT_CORNER_RADIUS = 0.5f;
static constexpr float TEE_WIDTH_ADJUSTMENT= 1.03f;
static constexpr float DEFAULT_ROAD_SCALE  = 8.0f;
static constexpr float MIN_ROAD_SEGMENT    = 0.25f;
static constexpr int   MAX_LINKS           = 6;
static constexpr float PI_F                = 3.14159265358979323846f;

static constexpr int MAX_SEG_VERTEX = 500;
static constexpr int MAX_SEG_INDEX  = 2000;

// ---- Simple 2D vector (replaces WW3D Vector2) ----
struct Vec2 {
    float X, Y;
    Vec2() : X(0), Y(0) {}
    Vec2(float x, float y) : X(x), Y(y) {}
    void Set(float x, float y) { X = x; Y = y; }
    void Set(const Vec2& v) { X = v.X; Y = v.Y; }
    Vec2 operator+(const Vec2& o) const { return {X+o.X, Y+o.Y}; }
    Vec2 operator-(const Vec2& o) const { return {X-o.X, Y-o.Y}; }
    Vec2 operator*(float s) const { return {X*s, Y*s}; }
    Vec2 operator/(float s) const { return {X/s, Y/s}; }
    Vec2 operator-() const { return {-X, -Y}; }
    Vec2& operator+=(const Vec2& o) { X+=o.X; Y+=o.Y; return *this; }
    Vec2& operator-=(const Vec2& o) { X-=o.X; Y-=o.Y; return *this; }
    Vec2& operator*=(float s) { X*=s; Y*=s; return *this; }
    bool operator==(const Vec2& o) const { return X==o.X && Y==o.Y; }
    bool operator!=(const Vec2& o) const { return !(*this==o); }
    float Length() const { return sqrtf(X*X + Y*Y); }
    void Normalize() { float l = Length(); if (l > 1e-10f) { X /= l; Y /= l; } }
    void Rotate(float angle) {
        float c = cosf(angle), s = sinf(angle);
        float nx = X*c - Y*s;
        float ny = X*s + Y*c;
        X = nx; Y = ny;
    }
    static float Dot_Product(const Vec2& a, const Vec2& b) { return a.X*b.X + a.Y*b.Y; }
};
inline Vec2 operator*(float s, const Vec2& v) { return {s*v.X, s*v.Y}; }

// ---- Simple 3D vector (replaces WW3D Vector3) ----
struct Vec3 {
    float X, Y, Z;
    Vec3() : X(0), Y(0), Z(0) {}
    Vec3(float x, float y, float z) : X(x), Y(y), Z(z) {}
    void Set(float x, float y, float z) { X = x; Y = y; Z = z; }
    Vec3 operator+(const Vec3& o) const { return {X+o.X, Y+o.Y, Z+o.Z}; }
    Vec3 operator-(const Vec3& o) const { return {X-o.X, Y-o.Y, Z-o.Z}; }
    Vec3 operator*(float s) const { return {X*s, Y*s, Z*s}; }
    Vec3& operator+=(const Vec3& o) { X+=o.X; Y+=o.Y; Z+=o.Z; return *this; }
    float Length() const { return sqrtf(X*X + Y*Y + Z*Z); }
    float Length2() const { return X*X + Y*Y + Z*Z; }
    void Normalize() { float l = Length(); if (l > 1e-10f) { X /= l; Y /= l; Z /= l; } }
    void Rotate_Z(float angle) {
        float c = cosf(angle), s = sinf(angle);
        float nx = X*c - Y*s;
        float ny = X*s + Y*c;
        X = nx; Y = ny;
    }
    static float Dot_Product(const Vec3& a, const Vec3& b) { return a.X*b.X + a.Y*b.Y + a.Z*b.Z; }
    static float Cross_Product_Z(const Vec3& a, const Vec3& b) { return a.X*b.Y - a.Y*b.X; }
    static float Distance(const Vec3& a, const Vec3& b) { return (a - b).Length(); }
};

// ---- Line segment (replaces WW3D LineSegClass) ----
struct LineSeg {
    Vec3 P0, P1;
    LineSeg() {}
    LineSeg(const Vec3& a, const Vec3& b) : P0(a), P1(b) {}
    void Set(const Vec3& a, const Vec3& b) { P0 = a; P1 = b; }
    Vec3 Get_Dir() const {
        Vec3 d = P1 - P0;
        d.Normalize();
        return d;
    }
    Vec3 Find_Point_Closest_To(const Vec3& pos) const;
    bool Find_Intersection(const LineSeg& other, Vec3* p1, float* f1, Vec3* p2, float* f2) const;
};

// ---- Corner enums ----
enum { bottomLeft=0, bottomRight=1, topLeft=2, topRight=3, NUM_CORNERS=4 };

enum TCorner : int {
    SEGMENT,
    CURVE,
    TEE,
    FOUR_WAY,
    THREE_WAY_Y,
    THREE_WAY_H,
    THREE_WAY_H_FLIP,
    ALPHA_JOIN,
    NUM_JOINS
};

// ---- Road point ----
struct TRoadPt {
    Vec2  loc;
    Vec2  top;
    Vec2  bottom;
    int   count;
    bool  last;
    bool  multi;
    bool  isAngled;
    bool  isJoin;
    TRoadPt() : count(0), last(true), multi(false), isAngled(false), isJoin(false) {}
};

// ---- Road segment info (for tessellation state) ----
struct TRoadSegInfo {
    Vec2 loc;
    Vec2 roadNormal;
    Vec2 roadVector;
    Vec2 corners[NUM_CORNERS];
    float uOffset;
    float vOffset;
    float scale;
};

// ---- Output vertex ----
struct RoadVertex {
    float x, y, z;
    uint32_t diffuse;
    float u, v;
};

// ---- Road segment (internal, mirrors original RoadSegment) ----
struct RoadSegment {
    TRoadPt  m_pt1;
    TRoadPt  m_pt2;
    float    m_curveRadius;
    TCorner  m_type;
    float    m_scale;
    float    m_widthInTexture;
    int      m_uniqueID;
    bool     m_visible;

    int      m_numVertex;
    RoadVertex* m_vb;
    int      m_numIndex;
    uint16_t* m_ib;
    TRoadSegInfo m_info;

    RoadSegment();
    ~RoadSegment();
    RoadSegment(const RoadSegment& o);
    RoadSegment& operator=(const RoadSegment& o);

    void SetVertexBuffer(RoadVertex* vb, int numVertex);
    void SetIndexBuffer(uint16_t* ib, int numIndex);
    void SetRoadSegInfo(TRoadSegInfo* pInfo) { m_info = *pInfo; }
    void GetRoadSegInfo(TRoadSegInfo* pInfo) { *pInfo = m_info; }
    int  GetNumVertex() const { return m_numVertex; }
    int  GetNumIndex()  const { return m_numIndex; }
    int  GetVertices(RoadVertex* dst, int n) const;
    int  GetIndices(uint16_t* dst, int n, int offset) const;
};

// ---- Road type (internal, mirrors original stacking info) ----
struct RoadTypeInfo {
    int   uniqueID;
    int   stackingOrder;
    float roadWidth;
    float roadWidthInTexture;
    std::string textureName;
    RoadTypeInfo() : uniqueID(-1), stackingOrder(0), roadWidth(DEFAULT_ROAD_SCALE), roadWidthInTexture(1.0f) {}
};

// ---- Output batch (one per road texture) ----
struct RoadBatch {
    std::vector<RoadVertex> vertices;
    std::vector<uint16_t>   indices;
    int         roadTypeID;
    std::string textureName;
};

// ---- Final output ----
struct RoadMeshOutput {
    std::vector<RoadBatch> batches;
};

// ---- Height map query interface (abstract) ----
// Callers provide an implementation that wraps WorldHeightMap.
struct IHeightMap {
    virtual ~IHeightMap() = default;
    virtual float getMaxCellHeight(float x, float y) const = 0;
    virtual int   getMaxHeightValue() const = 0;
    virtual int   getMinHeightValue() const = 0;
};

// ---- Concrete height map wrapper around WorldHeightMap ----
class WorldHeightMapAdapter : public IHeightMap {
    WorldHeightMap* m_map;
public:
    explicit WorldHeightMapAdapter(WorldHeightMap* map) : m_map(map) {}
    float getMaxCellHeight(float x, float y) const override;
    int   getMaxHeightValue() const override;
    int   getMinHeightValue() const override;
};

// ---- Main entry point ----
RoadMeshOutput GenerateRoadGeometry(WorldHeightMap* heightMap);

} // namespace RoadGeometry
