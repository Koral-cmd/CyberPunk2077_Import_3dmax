#include "xform.h"

#include <algorithm>

namespace cp77 {

Xf Xf::operator*(const Xf& b) const {
    Xf r;
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k) {
            double v = 0.0;
            for (int j = 0; j < 3; ++j) v += m[i][j] * b.m[j][k];
            if (i == 3) v += b.m[3][k];
            r.m[i][k] = v;
        }
    return r;
}

Vec3 Xf::point(const Vec3& p) const {
    return Vec3(p.x * m[0][0] + p.y * m[1][0] + p.z * m[2][0] + m[3][0],
                p.x * m[0][1] + p.y * m[1][1] + p.z * m[2][1] + m[3][1],
                p.x * m[0][2] + p.y * m[1][2] + p.z * m[2][2] + m[3][2]);
}
Vec3 Xf::vector(const Vec3& p) const {
    return Vec3(p.x * m[0][0] + p.y * m[1][0] + p.z * m[2][0],
                p.x * m[0][1] + p.y * m[1][1] + p.z * m[2][1],
                p.x * m[0][2] + p.y * m[1][2] + p.z * m[2][2]);
}

Xf Xf::inverse() const {
    const double a = m[0][0], b = m[0][1], c = m[0][2];
    const double d = m[1][0], e = m[1][1], f = m[1][2];
    const double g = m[2][0], h = m[2][1], i = m[2][2];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    Xf r;
    if (std::fabs(det) < 1e-30) return r;
    const double id = 1.0 / det;
    r.m[0][0] = A * id;              r.m[0][1] = -(b * i - c * h) * id; r.m[0][2] = (b * f - c * e) * id;
    r.m[1][0] = B * id;              r.m[1][1] = (a * i - c * g) * id;  r.m[1][2] = -(a * f - c * d) * id;
    r.m[2][0] = C * id;              r.m[2][1] = -(a * h - b * g) * id; r.m[2][2] = (a * e - b * d) * id;
    const Vec3 t = row(3);
    for (int k = 0; k < 3; ++k)
        r.m[3][k] = -(t.x * r.m[0][k] + t.y * r.m[1][k] + t.z * r.m[2][k]);
    return r;
}

Xf Xf::orthonormalized() const {
    Vec3 x = row(0), y = row(1);
    double lx = x.length();
    if (lx < 1e-12) x = Vec3(1, 0, 0); else x = x * (1.0 / lx);
    y = y - x * x.dot(y);
    double ly = y.length();
    if (ly < 1e-12) {
        y = std::fabs(x.x) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        y = y - x * x.dot(y);
        ly = y.length();
    }
    y = y * (1.0 / ly);
    Vec3 z = x.cross(y);
    if (z.dot(row(2)) < 0.0) z = z * -1.0;   // keep a mirrored basis mirrored
    Xf r;
    r.setRow(0, x);
    r.setRow(1, y);
    r.setRow(2, z);
    r.setRow(3, row(3));
    return r;
}

double Xf::maxDiff(const Xf& o) const {
    double d = 0.0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) d = std::max(d, std::fabs(m[r][c] - o.m[r][c]));
    return d;
}

Xf FromTRS(const Vec3& t, const Quatd& qin, const Vec3& s) {
    const Quatd q = qin.normalized();
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    // Column-vector rotation matrix R(q)
    const double R[3][3] = {
        { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w) },
        { 2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
        { 2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y) } };
    Xf m;
    const double sc[3] = { s.x, s.y, s.z };
    for (int r = 0; r < 3; ++r)          // row r of the row-vector matrix = column r of R, scaled
        for (int c = 0; c < 3; ++c) m.m[r][c] = R[c][r] * sc[r];
    m.setRow(3, t);
    return m;
}

Quatd RotationOf(const Xf& in) {
    const Xf o = in.orthonormalized();
    // Column-vector R = transpose of the row-vector basis.
    double R[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) R[r][c] = o.m[c][r];
    const double tr = R[0][0] + R[1][1] + R[2][2];
    Quatd q;
    if (tr > 0.0) {
        const double s = std::sqrt(tr + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (R[2][1] - R[1][2]) / s;
        q.y = (R[0][2] - R[2][0]) / s;
        q.z = (R[1][0] - R[0][1]) / s;
    } else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) {
        const double s = std::sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2.0;
        q.w = (R[2][1] - R[1][2]) / s;
        q.x = 0.25 * s;
        q.y = (R[0][1] + R[1][0]) / s;
        q.z = (R[0][2] + R[2][0]) / s;
    } else if (R[1][1] > R[2][2]) {
        const double s = std::sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2.0;
        q.w = (R[0][2] - R[2][0]) / s;
        q.x = (R[0][1] + R[1][0]) / s;
        q.y = 0.25 * s;
        q.z = (R[1][2] + R[2][1]) / s;
    } else {
        const double s = std::sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2.0;
        q.w = (R[1][0] - R[0][1]) / s;
        q.x = (R[0][2] + R[2][0]) / s;
        q.y = (R[1][2] + R[2][1]) / s;
        q.z = 0.25 * s;
    }
    return q.normalized();
}

Quatd Slerp(const Quatd& a, const Quatd& bIn, double t) {
    Quatd b = bIn;
    double d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0) { b = Quatd(-b.x, -b.y, -b.z, -b.w); d = -d; }
    if (d > 0.9995) {
        return Quatd(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t).normalized();
    }
    const double th = std::acos(d);
    const double s = std::sin(th);
    const double wa = std::sin((1.0 - t) * th) / s, wb = std::sin(t * th) / s;
    return Quatd(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb);
}
Vec3 Lerp(const Vec3& a, const Vec3& b, double t) { return a + (b - a) * t; }

Xf FromGltfMatrix(const float* a) {
    // Column-major array: a[4*col + row] of the column-vector matrix M.
    // Row-vector form is M^T: row i = column i = a[4i .. 4i+2].
    Xf m;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) m.m[r][c] = a[4 * r + c];
    return m;
}

Vec3 PointFromGltf(const Vec3& p) { return Vec3(p.x, -p.z, p.y); }

Xf FromGltf(const Xf& g) {
    // Row-vector C maps game -> glTF: rows (1,0,0), (0,0,-1), (0,1,0).
    // Game world W = C * G * C^T (C^T = C^-1, translation-free).
    Xf C;
    C.setRow(0, Vec3(1, 0, 0));
    C.setRow(1, Vec3(0, 0, -1));
    C.setRow(2, Vec3(0, 1, 0));
    C.setRow(3, Vec3(0, 0, 0));
    Xf Ci;
    Ci.setRow(0, Vec3(1, 0, 0));
    Ci.setRow(1, Vec3(0, 0, 1));
    Ci.setRow(2, Vec3(0, -1, 0));
    Ci.setRow(3, Vec3(0, 0, 0));
    return C * g * Ci;
}

Xf ScaledTranslation(const Xf& m, double s) {
    Xf r = m;
    r.setRow(3, m.translation() * s);
    return r;
}

} // namespace cp77
