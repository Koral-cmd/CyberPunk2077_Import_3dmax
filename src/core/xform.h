// ============================================================
//  Affine transforms in 3ds Max's convention.
//
//  Row vectors: p' = p * M. Rows 0..2 are the basis vectors,
//  row 3 is the translation - exactly the layout of Max's
//  Matrix3, so the plugin copies the 12 numbers 1:1.
//  Composition: world = local * parentWorld.
//
//  SPACES
//  ------
//  Game (REDengine): right-handed, Z up. That is also Max's
//  space, so game transforms go into Max unchanged (only the
//  translation is scaled from metres to system units).
//
//  glTF (what WolvenKit writes): right-handed, Y up.
//  WolvenKit maps game (x, y, z) -> glTF (x, z, -y) for points
//  and quaternions (i, j, k, r) -> (i, k, -j, r) (RigTools.cs).
//  The inverse used here: glTF (x, y, z) -> game (x, -z, y).
//  A transform is converted by similarity (FromGltf), so bone
//  frames, local keys and vertices all end up in one space.
// ============================================================
#pragma once
#include <cmath>

namespace cp77 {

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() {}
    Vec3(double a, double b, double c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x); }
    double length() const { return std::sqrt(dot(*this)); }
};

// Standard (right-handed, Hamilton) quaternion: x,y,z vector part, w scalar.
struct Quatd {
    double x = 0, y = 0, z = 0, w = 1;
    Quatd() {}
    Quatd(double a, double b, double c, double d) : x(a), y(b), z(c), w(d) {}
    Quatd normalized() const {
        const double n = std::sqrt(x * x + y * y + z * z + w * w);
        return n > 1e-12 ? Quatd(x / n, y / n, z / n, w / n) : Quatd();
    }
};

struct Xf {
    double m[4][3];
    Xf() { setIdentity(); }
    void setIdentity() {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 3; ++c) m[r][c] = (r == c) ? 1.0 : 0.0;
    }
    Vec3 row(int r) const { return Vec3(m[r][0], m[r][1], m[r][2]); }
    void setRow(int r, const Vec3& v) { m[r][0] = v.x; m[r][1] = v.y; m[r][2] = v.z; }
    Vec3 translation() const { return row(3); }

    Xf operator*(const Xf& b) const;            // this first, then b
    Vec3 point(const Vec3& p) const;            // p * M
    Vec3 vector(const Vec3& v) const;           // v * M (no translation)
    Xf inverse() const;                         // general affine inverse
    Xf orthonormalized() const;                 // strips scale/shear, keeps translation
    Vec3 scale() const { return Vec3(row(0).length(), row(1).length(), row(2).length()); }
    double maxDiff(const Xf& o) const;          // largest element difference
};

// T * R * S in row-vector form: rows = scaled columns of R(q).
Xf FromTRS(const Vec3& t, const Quatd& q, const Vec3& s);
Quatd RotationOf(const Xf& m);                  // from the (orthonormalized) basis
Quatd Slerp(const Quatd& a, const Quatd& b, double t);
Vec3 Lerp(const Vec3& a, const Vec3& b, double t);

// glTF column-major 16 floats (column-vector convention) -> Xf.
Xf FromGltfMatrix(const float* m16);

// Space conversion glTF -> game (= Max).
Vec3 PointFromGltf(const Vec3& p);              // (x, y, z) -> (x, -z, y)
Xf   FromGltf(const Xf& gltf);                  // similarity C * M * C^-1

// The game's own quaternion (i, j, k, r) is a standard quaternion
// in game space.
inline Quatd GameQuat(double i, double j, double k, double r) { return Quatd(i, j, k, r); }

// Scale only the translation row (metres -> Max system units).
Xf ScaledTranslation(const Xf& m, double s);

} // namespace cp77
