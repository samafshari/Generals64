// Windows .ANI animated cursor parser. See AniCursor.h for the why.
//
// Format reference:
//   .ANI is a RIFF file with form type 'ACON' and these chunks:
//     'anih' (36 bytes) — animation header
//     'rate' (optional) — 1 DWORD per step, jiffies override
//     'seq ' (optional) — 1 DWORD per step, frame index for each step
//     'LIST' 'fram' — container for the frame data
//       'icon' chunks — one per unique frame, each is a complete ICO/CUR
//
// ICO/CUR layout inside an icon chunk:
//   ICONDIR (6 bytes)         — reserved/type/count
//   ICONDIRENTRY (16 bytes)   — width, height, hotspot, byteCount, offset
//   BITMAPINFOHEADER (40 bytes) at offset
//   Color palette (4*N bytes for indexed depths <=8)
//   XOR image (BMP-style, bottom-up, row-padded to 4 bytes)
//   AND mask (1 bpp, bottom-up, row-padded to 4 bytes)
//
// Note: BITMAPINFOHEADER.biHeight is doubled (XOR + AND together) so
// the real image height is biHeight/2.

#include "AniCursor.h"

#include <cstring>

namespace AniCursor
{
namespace
{

// ─── Little-endian readers ────────────────────────────────────────────

inline uint16_t ReadU16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

inline uint32_t ReadU32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Match a 4-byte ASCII tag at p against `tag` (e.g. "RIFF").
inline bool TagEquals(const uint8_t* p, const char* tag)
{
    return p[0] == uint8_t(tag[0]) && p[1] == uint8_t(tag[1])
        && p[2] == uint8_t(tag[2]) && p[3] == uint8_t(tag[3]);
}

// ─── ICO/CUR decode ───────────────────────────────────────────────────
//
// Decode one cursor image (the embedded BMP inside one icon chunk) into
// `out`. Supports 4/8/24/32 bpp. The XOR pixels become BGRA; the AND
// mask is OR'd into the alpha channel (mask=1 → transparent for indexed
// formats; for 32-bit cursors that already carry alpha we leave their
// alpha alone unless every pixel is alpha=0, in which case we fall back
// to the AND-mask convention).

bool DecodeIcoFrame(const uint8_t* ico, size_t icoSize, Frame& out)
{
    if (icoSize < 6 + 16) return false;

    // ICONDIR
    const uint16_t reserved = ReadU16(ico + 0);
    const uint16_t type     = ReadU16(ico + 2);
    const uint16_t count    = ReadU16(ico + 4);
    if (reserved != 0) return false;
    if (type != 1 && type != 2) return false; // 1 = ICO, 2 = CUR
    if (count == 0) return false;

    // ICONDIRENTRY (use the first one — Generals .ANI puts one per icon
    // chunk anyway).
    const uint8_t* entry = ico + 6;
    int hotspotX = 0, hotspotY = 0;
    if (type == 2 /* CUR */)
    {
        hotspotX = ReadU16(entry + 4);
        hotspotY = ReadU16(entry + 6);
    }
    const uint32_t imageSize = ReadU32(entry + 8);
    const uint32_t imageOff  = ReadU32(entry + 12);
    if (imageOff + imageSize > icoSize) return false;
    if (imageSize < 40) return false;

    const uint8_t* dib = ico + imageOff;

    // BITMAPINFOHEADER
    const uint32_t hdrSize  = ReadU32(dib + 0);
    if (hdrSize < 40) return false;
    const int32_t  bmpW     = (int32_t)ReadU32(dib + 4);
    const int32_t  bmpHRaw  = (int32_t)ReadU32(dib + 8);
    const uint16_t planes   = ReadU16(dib + 12);
    const uint16_t bpp      = ReadU16(dib + 14);
    const uint32_t compress = ReadU32(dib + 16);
    uint32_t paletteCount   = ReadU32(dib + 32);

    if (planes != 1) return false;
    if (compress != 0 /* BI_RGB */) return false; // BI_BITFIELDS not seen in Generals
    if (bmpW <= 0) return false;
    // For ICO/CUR the height field is the COMBINED XOR + AND height.
    // Either positive (bottom-up) or negative (top-down) is technically
    // legal but Generals always uses positive bottom-up, which is what
    // we handle below.
    if (bmpHRaw <= 0) return false;
    const int width  = bmpW;
    const int height = bmpHRaw / 2;
    if (height <= 0) return false;

    // Default palette size for indexed depths.
    if (paletteCount == 0)
    {
        if (bpp == 1) paletteCount = 2;
        else if (bpp == 4) paletteCount = 16;
        else if (bpp == 8) paletteCount = 256;
    }

    const uint8_t* palette = dib + hdrSize;
    const uint32_t paletteBytes = (bpp <= 8) ? paletteCount * 4 : 0;

    const uint8_t* xorPixels = palette + paletteBytes;

    // Row stride helpers — BMP rows are padded to 4 bytes.
    auto RowStride = [](int w, int bitsPerPixel) -> size_t {
        return ((size_t(w) * bitsPerPixel + 31) / 32) * 4;
    };

    const size_t xorStride = RowStride(width, bpp);
    const size_t andStride = RowStride(width, 1);

    // Bounds check both planes.
    const size_t xorBytes = xorStride * size_t(height);
    const size_t andBytes = andStride * size_t(height);
    if (size_t(xorPixels - dib) + xorBytes + andBytes > imageSize)
        return false;

    const uint8_t* andPixels = xorPixels + xorBytes;

    out.width    = width;
    out.height   = height;
    out.hotspotX = hotspotX;
    out.hotspotY = hotspotY;
    out.bgra.assign(size_t(width) * size_t(height) * 4u, 0);

    // Walk pixels top-down. BMP is stored bottom-up so srcRow = (h-1-y).
    auto MaskBitAt = [&](int x, int y) -> bool {
        const uint8_t* row = andPixels + size_t(height - 1 - y) * andStride;
        return (row[x >> 3] >> (7 - (x & 7))) & 1;
    };

    // Detect "32-bit cursor with all-zero alpha" → fall back to mask
    // for alpha. Some authoring tools strip alpha but still emit 32bpp.
    bool any32Alpha = false;
    if (bpp == 32)
    {
        for (size_t i = 0; i < xorBytes; i += 4)
        {
            if (xorPixels[i + 3] != 0) { any32Alpha = true; break; }
        }
    }

    for (int y = 0; y < height; ++y)
    {
        const int srcY = height - 1 - y;
        const uint8_t* xorRow = xorPixels + size_t(srcY) * xorStride;
        uint8_t* dstRow = out.bgra.data() + size_t(y) * size_t(width) * 4u;

        for (int x = 0; x < width; ++x)
        {
            uint8_t b = 0, g = 0, r = 0, a = 0;

            if (bpp == 32)
            {
                b = xorRow[x * 4 + 0];
                g = xorRow[x * 4 + 1];
                r = xorRow[x * 4 + 2];
                a = any32Alpha ? xorRow[x * 4 + 3]
                               : (MaskBitAt(x, y) ? 0 : 0xFF);
            }
            else if (bpp == 24)
            {
                b = xorRow[x * 3 + 0];
                g = xorRow[x * 3 + 1];
                r = xorRow[x * 3 + 2];
                a = MaskBitAt(x, y) ? 0 : 0xFF;
            }
            else if (bpp == 8)
            {
                const uint8_t idx = xorRow[x];
                if (idx < paletteCount)
                {
                    b = palette[idx * 4 + 0];
                    g = palette[idx * 4 + 1];
                    r = palette[idx * 4 + 2];
                }
                a = MaskBitAt(x, y) ? 0 : 0xFF;
            }
            else if (bpp == 4)
            {
                const uint8_t byte = xorRow[x >> 1];
                const uint8_t idx  = (x & 1) ? (byte & 0x0F) : (byte >> 4);
                if (idx < paletteCount)
                {
                    b = palette[idx * 4 + 0];
                    g = palette[idx * 4 + 1];
                    r = palette[idx * 4 + 2];
                }
                a = MaskBitAt(x, y) ? 0 : 0xFF;
            }
            else if (bpp == 1)
            {
                const uint8_t byte = xorRow[x >> 3];
                const uint8_t idx  = (byte >> (7 - (x & 7))) & 1;
                if (idx < paletteCount)
                {
                    b = palette[idx * 4 + 0];
                    g = palette[idx * 4 + 1];
                    r = palette[idx * 4 + 2];
                }
                a = MaskBitAt(x, y) ? 0 : 0xFF;
            }
            else
            {
                return false; // unsupported bpp
            }

            dstRow[x * 4 + 0] = b;
            dstRow[x * 4 + 1] = g;
            dstRow[x * 4 + 2] = r;
            dstRow[x * 4 + 3] = a;
        }
    }

    return true;
}

// ─── RIFF walking ─────────────────────────────────────────────────────

struct Anih
{
    uint32_t cbSize;
    uint32_t nFrames;
    uint32_t nSteps;
    uint32_t iWidth;
    uint32_t iHeight;
    uint32_t iBitCount;
    uint32_t nPlanes;
    uint32_t iDispRate; // default jiffies per step
    uint32_t bfAttributes;
};

bool ParseAnih(const uint8_t* p, size_t size, Anih& out)
{
    if (size < 36) return false;
    out.cbSize       = ReadU32(p + 0);
    out.nFrames      = ReadU32(p + 4);
    out.nSteps       = ReadU32(p + 8);
    out.iWidth       = ReadU32(p + 12);
    out.iHeight      = ReadU32(p + 16);
    out.iBitCount    = ReadU32(p + 20);
    out.nPlanes      = ReadU32(p + 24);
    out.iDispRate    = ReadU32(p + 28);
    out.bfAttributes = ReadU32(p + 32);
    return out.nFrames > 0;
}

} // namespace

// ─── Public entry point ───────────────────────────────────────────────

bool Parse(const uint8_t* data, size_t size, Animation& out)
{
    out.frames.clear();
    out.sequence.clear();
    out.jiffies.clear();

    if (size < 12) return false;
    if (!TagEquals(data, "RIFF")) return false;
    if (!TagEquals(data + 8, "ACON")) return false;

    // The RIFF size field SHOULD be (file_size - 8) per spec, but some
    // .ANI files shipped with Generals/Zero Hour store it as the full
    // file size (off-by-8). Clamp to the real buffer end either way so
    // we don't reject valid files or walk past the end of broken ones.
    const uint32_t riffSize = ReadU32(data + 4);
    const size_t formEnd = (size_t(riffSize) + 8u <= size)
        ? size_t(riffSize) + 8u
        : size;

    Anih anih = {};
    bool gotAnih = false;

    // Walk top-level chunks inside the ACON form.
    const uint8_t* p   = data + 12;
    const uint8_t* end = data + formEnd;

    while (p + 8 <= end)
    {
        const uint8_t* chunkId = p;
        const uint32_t chunkSize = ReadU32(p + 4);
        const uint8_t* chunkData = p + 8;
        if (chunkData + chunkSize > end) return false;

        if (TagEquals(chunkId, "anih"))
        {
            if (!ParseAnih(chunkData, chunkSize, anih)) return false;
            gotAnih = true;
        }
        else if (TagEquals(chunkId, "rate"))
        {
            const uint32_t n = chunkSize / 4;
            out.jiffies.resize(n);
            for (uint32_t i = 0; i < n; ++i)
                out.jiffies[i] = ReadU32(chunkData + i * 4);
        }
        else if (TagEquals(chunkId, "seq "))
        {
            const uint32_t n = chunkSize / 4;
            out.sequence.resize(n);
            for (uint32_t i = 0; i < n; ++i)
                out.sequence[i] = ReadU32(chunkData + i * 4);
        }
        else if (TagEquals(chunkId, "LIST") && chunkSize >= 4
                 && TagEquals(chunkData, "fram"))
        {
            // Walk frame chunks within the LIST 'fram' container.
            const uint8_t* fp   = chunkData + 4;
            const uint8_t* fend = chunkData + chunkSize;
            while (fp + 8 <= fend)
            {
                const uint8_t* fid = fp;
                const uint32_t fsz = ReadU32(fp + 4);
                if (fp + 8 + fsz > fend) return false;
                if (TagEquals(fid, "icon"))
                {
                    Frame fr;
                    if (DecodeIcoFrame(fp + 8, fsz, fr))
                        out.frames.push_back(std::move(fr));
                }
                fp += 8 + fsz + (fsz & 1u); // word-align
            }
        }
        // (Ignore LIST/INFO and any other chunks.)

        p += 8 + chunkSize + (chunkSize & 1u); // word-align
    }

    if (!gotAnih || out.frames.empty()) return false;

    // Fill in defaults the .ANI may have omitted.
    if (out.sequence.empty())
    {
        // Identity: play frames 0..N-1 in order.
        out.sequence.resize(anih.nSteps > 0 ? anih.nSteps : out.frames.size());
        for (uint32_t i = 0; i < out.sequence.size(); ++i)
            out.sequence[i] = i % uint32_t(out.frames.size());
    }
    if (out.jiffies.empty())
    {
        out.jiffies.assign(out.sequence.size(),
                           anih.iDispRate > 0 ? anih.iDispRate : 6u /* ~10fps */);
    }
    // Both arrays must be the same length — if rate is shorter than seq,
    // pad it with the last value (or default jiffies).
    if (out.jiffies.size() < out.sequence.size())
    {
        const uint32_t pad = out.jiffies.empty()
            ? (anih.iDispRate > 0 ? anih.iDispRate : 6u)
            : out.jiffies.back();
        out.jiffies.resize(out.sequence.size(), pad);
    }
    // Clamp sequence indices that point past the end of frames[].
    for (uint32_t& idx : out.sequence)
    {
        if (idx >= out.frames.size())
            idx = uint32_t(out.frames.size()) - 1;
    }

    return true;
}

} // namespace AniCursor
