#import <CoreText/CoreText.h>
#import <CoreFoundation/CoreFoundation.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>
#import <math.h>
#import <sys/stat.h>
#import <mach-o/dyld.h>

#define GLYPHSVG_VERSION "1.1"

static CFDictionaryRef mappingsDict = NULL;

// Writes the directory containing the running executable into buf.
// Returns 1 on success, 0 on failure.
static int getExeDir(char *buf, size_t size) {
    char exePath[1024];
    uint32_t exePathSize = sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &exePathSize) != 0) {
        return 0;
    }
    char *slash = strrchr(exePath, '/');
    if (slash == NULL) {
        return 0;
    }
    *slash = '\0';
    strncpy(buf, exePath, size - 1);
    buf[size - 1] = '\0';
    return 1;
}

static int isRegularFile(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void loadMappingsDict(void) {
    char exeDir[1024];
    if (!getExeDir(exeDir, sizeof(exeDir))) {
        return;
    }
    char plistPath[1024];
    snprintf(plistPath, sizeof(plistPath), "%s/sfmap.plist", exeDir);
    FILE *plistFile = fopen(plistPath, "rb");
    if (plistFile == NULL) {
        return;
    }
    fseek(plistFile, 0, SEEK_END);
    size_t plistSize = ftell(plistFile);
    fseek(plistFile, 0, SEEK_SET);
    unsigned char *plistBytes = malloc(plistSize);
    if (plistBytes == NULL) {
        fclose(plistFile);
        return;
    }
    if (fread(plistBytes, 1, plistSize, plistFile) != plistSize) {
        free(plistBytes);
        fclose(plistFile);
        return;
    }
    fclose(plistFile);
    CFDataRef plistData = CFDataCreate(kCFAllocatorDefault, plistBytes, plistSize);
    free(plistBytes);
    if (plistData == NULL) {
        return;
    }
    CFErrorRef error = NULL;
    CFPropertyListRef plist = CFPropertyListCreateWithData(kCFAllocatorDefault, plistData, kCFPropertyListImmutable, NULL, &error);
    if (plist != NULL && CFGetTypeID(plist) == CFDictionaryGetTypeID()) {
        mappingsDict = (CFDictionaryRef)CFRetain(plist);
    }
    if (error != NULL) CFRelease(error);
    if (plist != NULL) CFRelease(plist);
    CFRelease(plistData);
}

static uint32_t getCodepointForName(const char *name) {
    if (mappingsDict == NULL) {
        fprintf(stderr, "Error: sfmap.plist not loaded\n");
        return 0;
    }
    CFStringRef key = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
    CFNumberRef value = (CFNumberRef)CFDictionaryGetValue(mappingsDict, key);
    CFRelease(key);
    if (value == NULL || CFGetTypeID(value) != CFNumberGetTypeID()) {
        return 0;
    }
    uint32_t codepoint;
    CFNumberGetValue(value, kCFNumberSInt32Type, &codepoint);
    return codepoint;
}

static const char *validWeights[] = {
    "black", "bold", "heavy", "light", "medium",
    "regular", "semibold", "thin", "ultralight", NULL
};

static int isValidWeight(const char *weight) {
    if (weight == NULL || weight[0] == '\0') return 1;
    for (int i = 0; validWeights[i] != NULL; i++) {
        if (strcasecmp(weight, validWeights[i]) == 0) return 1;
    }
    return 0;
}

// CTFontCreateWithName never fails: an unknown name silently yields the system
// default, which would then produce plausible SVGs from the wrong font. Ask the
// descriptor machinery whether the name matches anything installed, so callers
// can tell "not installed" from "installed" before trusting the result.
static int fontNameIsInstalled(CFStringRef name) {
    CTFontDescriptorRef requested = CTFontDescriptorCreateWithNameAndSize(name, 12.0);
    if (requested == NULL) {
        return 0;
    }
    CTFontDescriptorRef matched = CTFontDescriptorCreateMatchingFontDescriptor(requested, NULL);
    CFRelease(requested);
    if (matched == NULL) {
        return 0;
    }
    CFRelease(matched);
    return 1;
}

// Loads an installed font by PostScript/family name. Used by SF Symbols mode and
// by --font=<name>; fonts given as a path go through createFontFromPath instead.
static CTFontRef createFont(const char *customFont, const char *weight) {
    CTFontRef font = NULL;
    if (customFont != NULL) {
        CFStringRef fontNameCF = CFStringCreateWithCString(NULL, customFont, kCFStringEncodingUTF8);
        if (fontNameCF == NULL) {
            fprintf(stderr, "Error: Font '%s' is not valid UTF-8\n", customFont);
            return NULL;
        }
        if (!fontNameIsInstalled(fontNameCF)) {
            fprintf(stderr, "Error: Font '%s' not found\n", customFont);
            CFRelease(fontNameCF);
            return NULL;
        }
        font = CTFontCreateWithName(fontNameCF, 12.0, NULL);
        CFRelease(fontNameCF);
        if (font == NULL) {
            fprintf(stderr, "Error: Font '%s' not found\n", customFont);
            return NULL;
        }
    } else {
        if (!isValidWeight(weight)) {
            fprintf(stderr, "Error: Unknown weight '%s'\n", weight);
            fprintf(stderr, "Valid weights: black, bold, heavy, light, medium, regular, semibold, thin, ultralight\n");
            return NULL;
        }
        // Build SFProText-{Weight} PostScript name from weight argument
        char fontName[128];
        if (weight != NULL && weight[0] != '\0') {
            char capWeight[64];
            strncpy(capWeight, weight, sizeof(capWeight) - 1);
            capWeight[sizeof(capWeight) - 1] = '\0';
            if (capWeight[0] >= 'a' && capWeight[0] <= 'z') {
                capWeight[0] -= 32;
            }
            snprintf(fontName, sizeof(fontName), "SFProText-%s", capWeight);
        } else {
            snprintf(fontName, sizeof(fontName), "SFProText-Regular");
        }
        CFStringRef fontNameCF = CFStringCreateWithCString(NULL, fontName, kCFStringEncodingUTF8);
        // Only trust CTFontCreateWithName once the name is known to resolve.
        // Without this the call always "succeeds" with a substituted system font,
        // which left the SF Symbols fallback below unreachable on machines that
        // do not have SF Pro installed.
        if (fontNameCF != NULL && fontNameIsInstalled(fontNameCF)) {
            font = CTFontCreateWithName(fontNameCF, 12.0, NULL);
        }
        if (fontNameCF != NULL) CFRelease(fontNameCF);
        if (font == NULL) {
            // Fallback: try loading from SF Symbols app bundle
            CFURLRef fontURL = CFURLCreateWithFileSystemPath(NULL, CFSTR("/Applications/SF Symbols.app/Contents/Resources/Fonts/SFSymbolsFallback.otf"), kCFURLPOSIXPathStyle, false);
            CGDataProviderRef dataProvider = CGDataProviderCreateWithURL(fontURL);
            CFRelease(fontURL);
            if (dataProvider != NULL) {
                CGFontRef cgFont = CGFontCreateWithDataProvider(dataProvider);
                CGDataProviderRelease(dataProvider);
                if (cgFont != NULL) {
                    font = CTFontCreateWithGraphicsFont(cgFont, 12.0, NULL, NULL);
                    CGFontRelease(cgFont);
                }
            }
        }
        if (font == NULL) {
            fprintf(stderr, "Error: Font '%s' not found. Install SF Pro fonts or SF Symbols app.\n", fontName);
            return NULL;
        }
    }
    return font;
}

static void getCharLabel(const char *charString, int charIdx, uint32_t *outCp, char *outLabel, size_t labelSize) {
    *outCp = 0;
    outLabel[0] = '\0';

    if (charString == NULL) {
        return;
    }

    const unsigned char *p = (const unsigned char *)charString;
    int idx = 0;
    while (idx < charIdx && *p) {
        if ((*p & 0x80) == 0) {
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            p += 4;
        } else {
            p++;
        }
        idx++;
    }

    if (*p == '\0') {
        return;
    }

    if ((*p & 0x80) == 0) {
        *outCp = *p;
        snprintf(outLabel, labelSize, "U+%02X", *outCp);
    } else if ((*p & 0xE0) == 0xC0) {
        *outCp = ((uint32_t)(p[0] & 0x1F) << 6) | ((uint32_t)(p[1] & 0x3F));
        snprintf(outLabel, labelSize, "U+%02X", *outCp);
    } else if ((*p & 0xF0) == 0xE0) {
        *outCp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | ((uint32_t)(p[2] & 0x3F));
        snprintf(outLabel, labelSize, "U+%04X", *outCp);
    } else if ((*p & 0xF8) == 0xF0) {
        *outCp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) | ((uint32_t)(p[2] & 0x3F) << 6) | ((uint32_t)(p[3] & 0x3F));
        snprintf(outLabel, labelSize, "U+%06X", *outCp);
    }
}

static void writeSVG(FILE *fp, double size, CGRect bounds, const char *pathD) {
    fprintf(fp, "<svg width=\"%.0f\" height=\"%.0f\" viewBox=\"%.2f %.2f %.2f %.2f\" xmlns=\"http://www.w3.org/2000/svg\">\n",
            size, size, bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height);
    fprintf(fp, "  <path d=\"%s\" fill=\"currentColor\"/>\n", pathD);
    fprintf(fp, "</svg>\n");
}

static int countChars(const char *str) {
    int count = 0;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        if ((*p & 0x80) == 0) {
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            p += 4;
        } else {
            p++;
        }
        count++;
    }
    return count;
}

typedef struct {
    char *buffer;
    size_t *offset;
    size_t bufferSize;
    int truncated;
} PathToSVGContext;

// Function to apply to each element of the path
static void pathElementApplier(void *info, const CGPathElement *element) {
    PathToSVGContext *ctx = (PathToSVGContext *)info;
    size_t *offset = ctx->offset;
    char *buffer = ctx->buffer;
    size_t bufferSize = ctx->bufferSize;

    int n = 0;
    switch (element->type) {
        case kCGPathElementMoveToPoint:
            n = snprintf(buffer + *offset, bufferSize - *offset,
                         "M%.2f,%.2f ", element->points[0].x, element->points[0].y);
            break;
        case kCGPathElementAddLineToPoint:
            n = snprintf(buffer + *offset, bufferSize - *offset,
                         "L%.2f,%.2f ", element->points[0].x, element->points[0].y);
            break;
        case kCGPathElementAddQuadCurveToPoint:
            n = snprintf(buffer + *offset, bufferSize - *offset,
                         "Q%.2f,%.2f %.2f,%.2f ",
                         element->points[0].x, element->points[0].y,
                         element->points[1].x, element->points[1].y);
            break;
        case kCGPathElementAddCurveToPoint:
            n = snprintf(buffer + *offset, bufferSize - *offset,
                         "C%.2f,%.2f %.2f,%.2f %.2f,%.2f ",
                         element->points[0].x, element->points[0].y,
                         element->points[1].x, element->points[1].y,
                         element->points[2].x, element->points[2].y);
            break;
        case kCGPathElementCloseSubpath:
            n = snprintf(buffer + *offset, bufferSize - *offset, "Z ");
            break;
    }

    if (n < 0) {
        // Mark buffer as full to avoid further writes
        ctx->truncated = 1;
        *offset = ctx->bufferSize - 1;
    } else if ((size_t)n >= bufferSize - *offset) {
        // snprintf returns what it *would* have written, so this element did not
        // fit. Record it here rather than inferring truncation from a clamped
        // offset, which would also flag a path that filled the buffer exactly.
        ctx->truncated = 1;
        *offset = bufferSize - 1; // Leave room for null terminator
    } else {
        *offset += n;
    }
}

// Convert CGPath to SVG path string
// Renders a CGPath as an SVG path 'd' string. On return *outTruncated is 1 if the
// path did not fit the buffer, in which case the contents are unusable: a
// truncated 'd' attribute is a corrupt SVG, so callers must not write it out.
static char *pathToSVG(CGPathRef path, int *outTruncated) {
    // Use a static buffer for simplicity (not thread-safe, but acceptable for CLI tool)
    static char svgBuffer[50000];
    size_t svgOffset = 0;
    PathToSVGContext context = {svgBuffer, &svgOffset, sizeof(svgBuffer), 0};

    CGPathApply(path, &context, pathElementApplier);

    svgBuffer[svgOffset] = '\0';
    *outTruncated = context.truncated;
    if (context.truncated) {
        fprintf(stderr, "Error: SVG path data truncated (exceeded %zu bytes)\n", sizeof(svgBuffer));
    }

    return svgBuffer;
}

// ----- Fonts loaded by path, variable or static -----
//
// Everything below this point is shared by the icon-font modes (--set, --material,
// --font-file). Variable fonts (Google Material Symbols) and static fonts (MDI,
// Fluent) take the same route: load the file, ask the font which variation axes it
// declares, and apply only those. A static font declares none, so the axis requests
// fall away and the face itself decides the look.

#define AXIS_TAG_WGHT 0x77676874  // 'wght'
#define AXIS_TAG_FILL 0x46494C4C  // 'FILL'

// Two slots are held back for the wght and FILL requests every by-path render
// makes, so a long run of --axis flags cannot crowd them out silently.
#define MAX_AXIS_REQUESTS 16
#define MAX_USER_AXIS_REQUESTS (MAX_AXIS_REQUESTS - 2)

// A requested variation axis value. Axes the loaded font does not declare are
// dropped.
//
// The two flags answer two different questions and must not be merged:
//   explicitlyRequested - the user named a value, so `value` is theirs to honor.
//                         When it is 0 the axis is set to the font's own declared
//                         default instead, since this tool's idea of "regular"
//                         means nothing to a font whose wght axis runs 0.48-3.2.
//   answeredByFace      - a face of a static family already answered this request,
//                         so a font that cannot honor the axis is not worth a
//                         warning. It says nothing about whether `value` is real:
//                         a variable font in such a set must still move its axis.
typedef struct {
    uint32_t tag;
    double value;
    int explicitlyRequested;
    int answeredByFace;
    int applied;
} AxisRequest;

// Packs a four-character OpenType axis tag, right-padded with spaces.
// Returns 0 for an empty or over-long string.
static uint32_t axisTagFromString(const char *s) {
    size_t len = strlen(s);
    if (len == 0 || len > 4) return 0;
    uint32_t tag = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char c = (i < (int)len) ? (unsigned char)s[i] : ' ';
        tag = (tag << 8) | c;
    }
    return tag;
}

static void axisTagToString(uint32_t tag, char out[5]) {
    out[0] = (char)((tag >> 24) & 0xFF);
    out[1] = (char)((tag >> 16) & 0xFF);
    out[2] = (char)((tag >> 8) & 0xFF);
    out[3] = (char)(tag & 0xFF);
    out[4] = '\0';
}

static int hasAxisRequest(const AxisRequest *reqs, int n, uint32_t tag) {
    for (int i = 0; i < n; i++) {
        if (reqs[i].tag == tag) return 1;
    }
    return 0;
}

// Appends an axis request unless one for the same tag is already present, so an
// explicit --axis= always wins over the defaults derived from --weight/--fill.
static void addAxisRequest(AxisRequest *reqs, int *n, uint32_t tag, double value, int explicitlyRequested) {
    if (hasAxisRequest(reqs, *n, tag)) return;
    if (*n >= MAX_AXIS_REQUESTS) {
        char tagStr[5];
        axisTagToString(tag, tagStr);
        fprintf(stderr, "Warning: too many variation axes, dropping '%s'\n", tagStr);
        return;
    }
    reqs[*n].tag = tag;
    reqs[*n].value = value;
    reqs[*n].explicitlyRequested = explicitlyRequested;
    reqs[*n].answeredByFace = 0;
    reqs[*n].applied = 0;
    (*n)++;
}

// Marks an axis as already answered by a face selection, which suppresses the
// "font cannot honor this" warning without touching the requested value.
static void markAxisAnsweredByFace(AxisRequest *reqs, int n, uint32_t tag) {
    for (int i = 0; i < n; i++) {
        if (reqs[i].tag == tag) reqs[i].answeredByFace = 1;
    }
}

// Reads a CFNumber-valued key out of one entry of CTFontCopyVariationAxes.
static int axisNumber(CFDictionaryRef axis, CFStringRef key, double *out) {
    CFNumberRef num = (CFNumberRef)CFDictionaryGetValue(axis, key);
    if (num == NULL || CFGetTypeID(num) != CFNumberGetTypeID()) return 0;
    return CFNumberGetValue(num, kCFNumberDoubleType, out) ? 1 : 0;
}

// Adds a single variation axis (identified by its four-char tag) to a mutable
// variation dictionary.
static void setVariationAxis(CFMutableDictionaryRef variation, int32_t tag, double value) {
    CFNumberRef key = CFNumberCreate(NULL, kCFNumberSInt32Type, &tag);
    CFNumberRef val = CFNumberCreate(NULL, kCFNumberDoubleType, &value);
    CFDictionarySetValue(variation, key, val);
    CFRelease(key);
    CFRelease(val);
}

// Fills variation with the subset of reqs the font actually declares, clamping
// each value to that axis's own range. Returns how many were applied and reports
// the number of axes the font declares through outDeclaredAxes (0 for a static
// font). Marks the applied requests so the caller can warn about the rest.
static int buildVariationDict(CTFontRef font, AxisRequest *reqs, int nReqs,
                              CFMutableDictionaryRef variation, int *outDeclaredAxes) {
    if (outDeclaredAxes != NULL) *outDeclaredAxes = 0;
    CFArrayRef axes = CTFontCopyVariationAxes(font);
    if (axes == NULL) return 0;
    CFIndex nAxes = CFArrayGetCount(axes);
    if (outDeclaredAxes != NULL) *outDeclaredAxes = (int)nAxes;

    int applied = 0;
    for (CFIndex i = 0; i < nAxes; i++) {
        CFDictionaryRef axis = (CFDictionaryRef)CFArrayGetValueAtIndex(axes, i);
        if (CFGetTypeID(axis) != CFDictionaryGetTypeID()) continue;
        double identifier = 0;
        if (!axisNumber(axis, kCTFontVariationAxisIdentifierKey, &identifier)) continue;
        uint32_t tag = (uint32_t)identifier;
        for (int r = 0; r < nReqs; r++) {
            if (reqs[r].tag != tag) continue;
            double value = reqs[r].value;
            // The wght and FILL requests are made for every by-path render so
            // that a font declaring them always lands on a defined instance.
            // When the user did not actually ask, that instance has to be the
            // font's own default: this tool's notion of "regular" is 400, which
            // is meaningless to a font whose wght axis runs 0.48 to 3.2.
            double dflt = 0;
            if (!reqs[r].explicitlyRequested &&
                axisNumber(axis, kCTFontVariationAxisDefaultValueKey, &dflt)) {
                value = dflt;
            }
            double limit = 0;
            if (axisNumber(axis, kCTFontVariationAxisMinimumValueKey, &limit) && value < limit) value = limit;
            if (axisNumber(axis, kCTFontVariationAxisMaximumValueKey, &limit) && value > limit) value = limit;
            setVariationAxis(variation, (int32_t)tag, value);
            reqs[r].applied = 1;
            applied++;
            break;
        }
    }
    CFRelease(axes);
    return applied;
}

// Loads a font from a file and applies the requested variation axes. Works for
// both variable and static fonts: a static font declares no axes, so nothing is
// applied and the default instance is returned. outIsVariable, when non-NULL,
// reports whether the font declares any axes at all.
static CTFontRef createFontFromPath(const char *path, AxisRequest *reqs, int nReqs, int *outIsVariable) {
    if (outIsVariable != NULL) *outIsVariable = 0;

    CFStringRef pathStr = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
    if (pathStr == NULL) return NULL;
    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, pathStr, kCFURLPOSIXPathStyle, false);
    CFRelease(pathStr);
    if (url == NULL) return NULL;

    CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromURL(url);
    CFRelease(url);
    if (descriptors == NULL || CFArrayGetCount(descriptors) == 0) {
        if (descriptors != NULL) CFRelease(descriptors);
        fprintf(stderr, "Error: Could not load font from %s\n", path);
        return NULL;
    }
    CTFontDescriptorRef baseDesc = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descriptors, 0);
    // The family name the file declares, kept so the font CoreText hands back can
    // be checked against it below. Family name, not PostScript name: instantiating
    // a variable font at a given axis position legitimately changes the PostScript
    // name (the descriptor reports the default named instance), while the family
    // stays put.
    CFStringRef wantedName = CTFontDescriptorCopyAttribute(baseDesc, kCTFontFamilyNameAttribute);
    if (wantedName != NULL && CFGetTypeID(wantedName) != CFStringGetTypeID()) {
        CFRelease(wantedName);
        wantedName = NULL;
    }

    CTFontRef font = CTFontCreateWithFontDescriptor(baseDesc, 12.0, NULL);
    if (font == NULL) {
        CFRelease(descriptors);
        if (wantedName != NULL) CFRelease(wantedName);
        fprintf(stderr, "Error: Could not instantiate font from %s\n", path);
        return NULL;
    }

    int declaredAxes = 0;
    CFMutableDictionaryRef variation = CFDictionaryCreateMutable(NULL, MAX_AXIS_REQUESTS,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    int applied = buildVariationDict(font, reqs, nReqs, variation, &declaredAxes);
    if (outIsVariable != NULL) *outIsVariable = (declaredAxes > 0);

    if (applied > 0) {
        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(NULL, 1,
                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(attrs, kCTFontVariationAttribute, variation);
        CTFontDescriptorRef varDesc = CTFontDescriptorCreateCopyWithAttributes(baseDesc, attrs);
        CFRelease(attrs);
        if (varDesc != NULL) {
            CTFontRef varFont = CTFontCreateWithFontDescriptor(varDesc, 12.0, NULL);
            CFRelease(varDesc);
            if (varFont != NULL) {
                CFRelease(font);
                font = varFont;
            } else {
                fprintf(stderr, "Warning: Could not instantiate the varied font, using the default instance\n");
            }
        } else {
            fprintf(stderr, "Warning: Could not apply variation to font, using the default instance\n");
        }
    }
    CFRelease(variation);
    CFRelease(descriptors);

    // Descriptors from CTFontManagerCreateFontDescriptorsFromURL are not registered
    // for descriptor matching, so a CoreText that declines this one substitutes a
    // system font rather than failing. That font has glyphs at the wrong codepoints,
    // so it would yield wrong output instead of an error. Verify the identity.
    // Checked on the font actually returned, which covers both the base instance
    // and the varied one.
    if (wantedName != NULL) {
        CFStringRef gotName = CTFontCopyFamilyName(font);
        if (gotName == NULL || CFStringCompare(gotName, wantedName, 0) != kCFCompareEqualTo) {
            char got[256] = "(unknown)";
            char wanted[256] = "(unknown)";
            if (gotName != NULL) CFStringGetCString(gotName, got, sizeof(got), kCFStringEncodingUTF8);
            CFStringGetCString(wantedName, wanted, sizeof(wanted), kCFStringEncodingUTF8);
            fprintf(stderr, "Error: CoreText substituted font family '%s' for '%s' loaded from %s\n", got, wanted, path);
            if (gotName != NULL) CFRelease(gotName);
            CFRelease(wantedName);
            CFRelease(font);
            return NULL;
        }
        CFRelease(gotName);
        CFRelease(wantedName);
    }

    for (int r = 0; r < nReqs; r++) {
        if (reqs[r].applied || !reqs[r].explicitlyRequested || reqs[r].answeredByFace) continue;
        char tag[5];
        axisTagToString(reqs[r].tag, tag);
        if (declaredAxes == 0) {
            fprintf(stderr, "Warning: static font (no variation axes); requested %s=%g ignored\n", tag, reqs[r].value);
        } else {
            fprintf(stderr, "Warning: font has no '%s' axis; requested value %g ignored\n", tag, reqs[r].value);
        }
    }
    return font;
}

// Prints "axis: <tag> <min> <default> <max> <name>" for every axis in an array
// returned by CTFontCopyVariationAxes. The caller owns the array, because it
// also needs the axis count before these lines are printed.
static void printAxisLines(CFArrayRef axes) {
    if (axes == NULL) return;
    CFIndex nAxes = CFArrayGetCount(axes);
    for (CFIndex i = 0; i < nAxes; i++) {
        CFDictionaryRef axis = (CFDictionaryRef)CFArrayGetValueAtIndex(axes, i);
        if (CFGetTypeID(axis) != CFDictionaryGetTypeID()) continue;
        double identifier = 0, minValue = 0, defValue = 0, maxValue = 0;
        if (!axisNumber(axis, kCTFontVariationAxisIdentifierKey, &identifier)) continue;
        axisNumber(axis, kCTFontVariationAxisMinimumValueKey, &minValue);
        axisNumber(axis, kCTFontVariationAxisDefaultValueKey, &defValue);
        axisNumber(axis, kCTFontVariationAxisMaximumValueKey, &maxValue);
        char tag[5];
        axisTagToString((uint32_t)identifier, tag);

        char axisName[128] = {0};
        CFStringRef nameRef = (CFStringRef)CFDictionaryGetValue(axis, kCTFontVariationAxisNameKey);
        if (nameRef != NULL && CFGetTypeID(nameRef) == CFStringGetTypeID()) {
            CFStringGetCString(nameRef, axisName, sizeof(axisName), kCFStringEncodingUTF8);
        }
        printf("axis: %s %g %g %g %s\n", tag, minValue, defValue, maxValue, axisName);
    }
}

// ----- Weight and fill inputs -----

typedef struct { const char *name; double value; } WeightName;

// SF Symbols-style weight names mapped to approximate numeric font weights
// (Apple SF Pro numerics). Each font clamps these to its own wght axis range,
// so heavy and black land on the maximum of a narrower axis.
static const WeightName weightNames[] = {
    {"ultralight", 100}, {"thin", 200}, {"light", 300}, {"regular", 400},
    {"medium", 500}, {"semibold", 600}, {"bold", 700}, {"heavy", 800},
    {"black", 900}, {NULL, 0}
};

static double weightNameToValue(const char *name) {
    if (name == NULL) return 0;
    for (int i = 0; weightNames[i].name != NULL; i++) {
        if (strcasecmp(name, weightNames[i].name) == 0) {
            return weightNames[i].value;
        }
    }
    return 0;
}


// Resolves the numeric wght axis value from the explicit --weight= flag (numeric
// or named) and/or a positional named weight, defaulting to regular (400).
// Clamping to the font's actual axis range happens at apply time.
static double resolveWeightValue(const char *weightFlag, const char *weightName) {
    if (weightFlag != NULL && weightFlag[0] != '\0') {
        char *end;
        double v = strtod(weightFlag, &end);
        if (*end == '\0' && !isnan(v)) {
            return v;
        }
        double nv = weightNameToValue(weightFlag);
        if (nv > 0) return nv;
        fprintf(stderr, "Warning: unknown --weight '%s', using regular (400)\n", weightFlag);
        return 400.0;
    }
    if (weightName != NULL && weightName[0] != '\0') {
        double nv = weightNameToValue(weightName);
        if (nv > 0) return nv;
        fprintf(stderr, "Warning: unknown weight '%s', using regular (400)\n", weightName);
        return 400.0;
    }
    return 400.0;
}

// Resolves the FILL axis value (0-1). The --fill flag may be given bare (treated
// as 1.0) or as --fill=<value>; absent means 0.0 (outline).
static double resolveFillValue(const char *fillFlag) {
    if (fillFlag == NULL) return 0.0;
    if (fillFlag[0] == '\0') return 1.0; // bare --fill
    char *end;
    double v = strtod(fillFlag, &end);
    if (*end != '\0' || isnan(v)) {
        fprintf(stderr, "Warning: invalid --fill '%s', using filled (1)\n", fillFlag);
        return 1.0;
    }
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

// ----- Glyph sets -----
//
// A glyph set is a directory holding one or more icon-font faces plus the
// name-to-codepoint tables that go with them. One face means one font file; a
// variable font uses its axes for weight and fill, while a static family ships a
// separate face per weight and selects between them by name.

#define MAX_FACES 12
#define MAX_SET_ROOTS 16
#define GLYPHSET_MANIFEST "glyphset.conf"

typedef struct {
    char name[64];
    char fontFile[256];
    char codepointsFile[256];
} GlyphFace;

typedef struct {
    char name[64];
    char dir[1024];
    char title[128];
    char license[128];    // one-line license name, for a caller to show beside the set
    char kind[16];         // "icon" or "text"; what the symbols are, not how they render
    char metadataFile[256];
    char defaultFace[64]; // face to prefer when none is requested; may be absent
    GlyphFace faces[MAX_FACES];
    int nFaces;
} GlyphSet;

// Google Material Symbols ships three same-shaped variable fonts, one per style.
// It predates the manifest, so its layout is built in and an existing material
// directory (including one already provisioned into an app bundle) needs no
// glyphset.conf.
static const char *materialStyles[] = { "Outlined", "Rounded", "Sharp", NULL };

// A manifest may have supplied a title or metadata name already; only the parts
// it left empty are filled in here.
static void fillBuiltinMaterialSet(GlyphSet *set) {
    if (set->title[0] == '\0') {
        snprintf(set->title, sizeof(set->title), "Google Material Symbols");
    }
    if (set->metadataFile[0] == '\0') {
        snprintf(set->metadataFile, sizeof(set->metadataFile), "material_symbols_metadata.json");
    }
    if (set->license[0] == '\0') {
        snprintf(set->license, sizeof(set->license), "Apache License 2.0");
    }
    if (set->kind[0] == '\0') snprintf(set->kind, sizeof(set->kind), "icon");
    if (set->defaultFace[0] == '\0') {
        snprintf(set->defaultFace, sizeof(set->defaultFace), "%s", materialStyles[0]);
    }
    for (int i = 0; materialStyles[i] != NULL && set->nFaces < MAX_FACES; i++) {
        GlyphFace *face = &set->faces[set->nFaces++];
        snprintf(face->name, sizeof(face->name), "%s", materialStyles[i]);
        snprintf(face->fontFile, sizeof(face->fontFile), "MaterialSymbols%s.ttf", materialStyles[i]);
        snprintf(face->codepointsFile, sizeof(face->codepointsFile), "MaterialSymbols%s.codepoints", materialStyles[i]);
    }
}

static char *trimWhitespace(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    *end = '\0';
    return s;
}

// Splits off the next whitespace-delimited token, advancing *cursor past it.
// Returns NULL when the string is exhausted.
static char *nextToken(char **cursor) {
    char *s = *cursor;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') {
        *cursor = s;
        return NULL;
    }
    char *start = s;
    while (*s != '\0' && *s != ' ' && *s != '\t') s++;
    if (*s != '\0') {
        *s = '\0';
        s++;
    }
    *cursor = s;
    return start;
}

// Parses <dir>/glyphset.conf. One "key = value" per line, '#' starts a comment:
//
//   title      = human readable set name
//   license    = one-line license name, e.g. "SIL Open Font License 1.1"
//   kind       = "icon" (named pictograms) or "text" (characters); default "icon"
//   font       = font filename, for a set with a single face
//   codepoints = default codepoints filename, used by faces that omit their own
//   metadata   = optional search metadata filename (for callers, not read here)
//   default    = face to use when none is requested (default: the first face)
//   face       = <name> <fontfile> [codepointsfile]
//
// Filenames may not contain spaces. Returns 1 if the manifest declared at least
// one face; a manifest that only overrides, say, the title leaves the caller free
// to fall back to a built-in layout.
static int parseSetManifest(const char *dir, GlyphSet *set) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", dir, GLYPHSET_MANIFEST);
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 0;

    char defaultCodepoints[256] = {0};
    char singleFont[256] = {0};
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *comment = strchr(line, '#');
        if (comment != NULL) *comment = '\0';
        char *eq = strchr(line, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        char *key = trimWhitespace(line);
        char *value = trimWhitespace(eq + 1);
        if (key[0] == '\0' || value[0] == '\0') continue;

        if (strcasecmp(key, "face") == 0) {
            if (set->nFaces >= MAX_FACES) {
                fprintf(stderr, "Warning: %s declares more than %d faces, ignoring the rest\n", path, MAX_FACES);
                continue;
            }
            char *cursor = value;
            const char *faceName = nextToken(&cursor);
            const char *faceFont = nextToken(&cursor);
            const char *faceCodepoints = nextToken(&cursor);
            if (faceName == NULL || faceFont == NULL) {
                fprintf(stderr, "Warning: %s: 'face' needs '<name> <fontfile> [codepointsfile]'\n", path);
                continue;
            }
            if (strlen(faceName) >= sizeof(set->faces[0].name)) {
                fprintf(stderr, "Warning: %s: face name '%s' is truncated to %zu characters\n",
                        path, faceName, sizeof(set->faces[0].name) - 1);
            }
            GlyphFace *face = &set->faces[set->nFaces++];
            snprintf(face->name, sizeof(face->name), "%s", faceName);
            snprintf(face->fontFile, sizeof(face->fontFile), "%s", faceFont);
            if (faceCodepoints != NULL) {
                snprintf(face->codepointsFile, sizeof(face->codepointsFile), "%s", faceCodepoints);
            }
        } else if (strcasecmp(key, "default") == 0) {
            snprintf(set->defaultFace, sizeof(set->defaultFace), "%s", value);
        } else if (strcasecmp(key, "font") == 0) {
            snprintf(singleFont, sizeof(singleFont), "%s", value);
        } else if (strcasecmp(key, "codepoints") == 0) {
            snprintf(defaultCodepoints, sizeof(defaultCodepoints), "%s", value);
        } else if (strcasecmp(key, "metadata") == 0) {
            snprintf(set->metadataFile, sizeof(set->metadataFile), "%s", value);
        } else if (strcasecmp(key, "title") == 0) {
            snprintf(set->title, sizeof(set->title), "%s", value);
        } else if (strcasecmp(key, "license") == 0) {
            snprintf(set->license, sizeof(set->license), "%s", value);
        } else if (strcasecmp(key, "kind") == 0) {
            // Anything else is a typo, and silently accepting it would put an
            // unknown word where a caller expects one of two.
            // Stored lowercased, not as written: the comparison here is
            // case-blind, so accepting "ICON" and then reporting it verbatim
            // would hand a caller a value its own comparison misses - and the
            // symptom is a set quietly sorted into the wrong group, not an error.
            if (strcasecmp(value, "icon") == 0) {
                snprintf(set->kind, sizeof(set->kind), "icon");
            } else if (strcasecmp(value, "text") == 0) {
                snprintf(set->kind, sizeof(set->kind), "text");
            } else {
                fprintf(stderr, "Warning: %s: 'kind' is 'icon' or 'text', not '%s'; using icon\n",
                        path, value);
            }
        }
    }
    fclose(fp);

    // A set with one font needs no face line; give it a face named "regular".
    if (set->nFaces == 0 && singleFont[0] != '\0') {
        GlyphFace *face = &set->faces[set->nFaces++];
        snprintf(face->name, sizeof(face->name), "regular");
        snprintf(face->fontFile, sizeof(face->fontFile), "%s", singleFont);
    }
    for (int i = 0; i < set->nFaces; i++) {
        if (set->faces[i].codepointsFile[0] == '\0') {
            snprintf(set->faces[i].codepointsFile, sizeof(set->faces[i].codepointsFile), "%s", defaultCodepoints);
        }
    }
    // Every set that predates this key is an icon font, so that is the default a
    // silent manifest means.
    if (set->kind[0] == '\0') snprintf(set->kind, sizeof(set->kind), "icon");
    return set->nFaces > 0;
}

// Populates set from a directory: the manifest first, the built-in Material
// layout as a fallback. Faces whose font file is absent are dropped, so a partial
// install (ICEdit bundles only the Rounded style) still resolves.
// Returns 1 if the directory holds at least one usable face.
static int loadGlyphSet(const char *setName, const char *dir, GlyphSet *set) {
    memset(set, 0, sizeof(*set));
    snprintf(set->name, sizeof(set->name), "%s", setName);
    snprintf(set->dir, sizeof(set->dir), "%s", dir);

    if (!parseSetManifest(dir, set)) {
        if (strcasecmp(setName, "material") != 0) return 0;
        fillBuiltinMaterialSet(set);
    }

    // Checked against set->dir, the same buffer the render later builds from.
    int kept = 0;
    for (int i = 0; i < set->nFaces; i++) {
        char fontPath[1400];
        snprintf(fontPath, sizeof(fontPath), "%s/%s", set->dir, set->faces[i].fontFile);
        if (!isRegularFile(fontPath)) continue;
        if (kept != i) set->faces[kept] = set->faces[i];
        kept++;
    }
    set->nFaces = kept;
    return set->nFaces > 0;
}

// Directories that may hold per-set subdirectories: the entries of
// GLYPHSVG_SET_PATH, then the current directory. Deliberately nothing else - no
// walking up from the executable, no ~/Library. Pointing at the right data is the
// caller's job, and a tool that guesses at other locations is one that renders
// from a set nobody asked for. A caller that knows where its set lives passes
// --set=<dir>, or names the directory in GLYPHSVG_SET_PATH.
static int collectSetRoots(char roots[][1024], int maxRoots) {
    int n = 0;
    const char *envPath = getenv("GLYPHSVG_SET_PATH");
    if (envPath != NULL && envPath[0] != '\0') {
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s", envPath);
        char *cursor = buf;
        char *item;
        while ((item = strsep(&cursor, ":")) != NULL) {
            if (item[0] == '\0') continue;
            // One slot is held back for the current directory, so a long
            // GLYPHSVG_SET_PATH cannot crowd it out and look like a missing set.
            if (n >= maxRoots - 1) {
                fprintf(stderr, "Warning: GLYPHSVG_SET_PATH has more than %d entries, ignoring the rest\n", n);
                break;
            }
            snprintf(roots[n++], 1024, "%s", item);
        }
    }
    if (n < maxRoots) snprintf(roots[n++], 1024, ".");
    return n;
}

static const GlyphFace *findFace(const GlyphSet *set, const char *faceName) {
    if (faceName == NULL || faceName[0] == '\0') return NULL;
    for (int i = 0; i < set->nFaces; i++) {
        if (strcasecmp(faceName, set->faces[i].name) == 0) return &set->faces[i];
    }
    return NULL;
}

// Nearest face by weight, measured against the faces this set actually has: a
// face name that is a weight name carries that weight. Returns NULL unless EVERY
// face is named for a weight, because a set is either indexed by weight or it is
// not. One weight-named face among styles (Outlined, Rounded, bold) would
// otherwise capture every weight at any distance, so asking for the lightest
// would hand back that one face.
static const GlyphFace *findFaceNearestWeight(const GlyphSet *set, double weightValue) {
    for (int i = 0; i < set->nFaces; i++) {
        if (weightNameToValue(set->faces[i].name) <= 0) return NULL;
    }
    const GlyphFace *best = NULL;
    double bestDistance = 0;
    double bestWeight = 0;
    for (int i = 0; i < set->nFaces; i++) {
        double faceWeight = weightNameToValue(set->faces[i].name);
        if (faceWeight <= 0) continue;
        double distance = fabs(weightValue - faceWeight);
        // Exactly between two faces, take the lighter one, so the answer does
        // not depend on the order the manifest happens to list them in.
        if (best == NULL || distance < bestDistance ||
            (distance == bestDistance && faceWeight < bestWeight)) {
            best = &set->faces[i];
            bestDistance = distance;
            bestWeight = faceWeight;
        }
    }
    return best;
}

// Whether this directory's set can serve the face the caller wants, by any of
// the spellings that select one: --face, or a weight that names a face. Root
// selection consults it so a partial install does not shadow a complete one
// further down the search path: the old per-style Material lookup fell through
// to the next candidate when a style was missing, and so must this.
static int setHasRequestedFace(const GlyphSet *set, const char *faceArg,
                               const char *weightSpec, double weightValue) {
    if (faceArg != NULL && faceArg[0] != '\0') return findFace(set, faceArg) != NULL;
    if (weightSpec != NULL && weightSpec[0] != '\0') {
        if (findFace(set, weightSpec) != NULL) return 1;
        if (findFaceNearestWeight(set, weightValue) != NULL) {
            // This set indexes its faces by weight, so a root holding an exact
            // face for the requested weight should win over one that only has a
            // near miss. Nearest-neighbor is the last resort, not a reason to
            // stop searching.
            return 0;
        }
        // Otherwise the weight is bound for the wght axis, not for a face, and
        // says nothing about which directory to use.
    }
    if (set->defaultFace[0] != '\0') return findFace(set, set->defaultFace) != NULL;
    return 1;
}

#define MAX_SET_CANDIDATES (MAX_SET_ROOTS + 1)

// Finds a set by name, preferring a directory that has the requested face. A
// name containing '/' is taken as a literal directory. GLYPHSVG_MATERIAL_DIR
// stays honored for the material set.
static int resolveGlyphSet(const char *setName, const char *faceArg,
                           const char *weightSpec, double weightValue, GlyphSet *set) {
    char candidates[MAX_SET_CANDIDATES][1200];
    int nCandidates = 0;
    char displayName[64];
    snprintf(displayName, sizeof(displayName), "%s", setName);

    if (strchr(setName, '/') != NULL) {
        char *dir = candidates[nCandidates++];
        snprintf(dir, 1200, "%s", setName);
        size_t len = strlen(dir);
        while (len > 1 && dir[len - 1] == '/') dir[--len] = '\0';
        const char *slash = strrchr(dir, '/');
        snprintf(displayName, sizeof(displayName), "%s",
                 (slash != NULL && slash[1] != '\0') ? slash + 1 : dir);
    } else {
        if (strcasecmp(setName, "material") == 0) {
            const char *env = getenv("GLYPHSVG_MATERIAL_DIR");
            if (env != NULL && env[0] != '\0') {
                snprintf(candidates[nCandidates++], 1200, "%s", env);
            }
        }
        char roots[MAX_SET_ROOTS][1024];
        int nRoots = collectSetRoots(roots, MAX_SET_ROOTS);
        for (int i = 0; i < nRoots && nCandidates < MAX_SET_CANDIDATES; i++) {
            snprintf(candidates[nCandidates++], 1200, "%s/%s", roots[i], setName);
        }
    }

    // Pass 0 takes the first candidate that has the requested face; pass 1
    // accepts any usable set, so an unknown face still reports itself against a
    // real face list instead of as a missing set.
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nCandidates; i++) {
            if (!loadGlyphSet(displayName, candidates[i], set)) continue;
            if (pass == 0 && !setHasRequestedFace(set, faceArg, weightSpec, weightValue)) continue;
            return 1;
        }
    }
    return 0;
}

static void printFaceList(FILE *fp, const GlyphSet *set) {
    for (int i = 0; i < set->nFaces; i++) {
        fprintf(fp, "%s%s", (i > 0) ? " " : "", set->faces[i].name);
    }
    fprintf(fp, "\n");
}


// Picks the face to render with. An explicit --face/--style wins, and an unknown
// one is an error (NULL). Otherwise the requested weight is tried against the
// face names, first as given and then as the nearest weight the set offers,
// which is how a static family shipping Light/Regular/Bold as separate files
// honors a weight. outWeightConsumed reports whether the weight was answered
// that way, so the caller can stay quiet about a static font ignoring the wght
// axis. It does NOT mean the weight has been spent: the wght request carries the
// user's value regardless, so a variable font in such a set still moves its axis.
static const GlyphFace *resolveFace(const GlyphSet *set, const char *faceArg,
                                    const char *weightSpec, double weightValue,
                                    int *outWeightConsumed) {
    if (outWeightConsumed != NULL) *outWeightConsumed = 0;
    if (set->nFaces == 0) return NULL;

    if (faceArg != NULL && faceArg[0] != '\0') {
        return findFace(set, faceArg);
    }
    if (weightSpec != NULL && weightSpec[0] != '\0') {
        const GlyphFace *face = findFace(set, weightSpec);
        if (face == NULL) face = findFaceNearestWeight(set, weightValue);
        if (face != NULL) {
            if (outWeightConsumed != NULL) *outWeightConsumed = 1;
            return face;
        }
    }
    if (set->defaultFace[0] != '\0') {
        const GlyphFace *face = findFace(set, set->defaultFace);
        if (face != NULL) return face;
        fprintf(stderr, "Warning: set '%s' has no '%s' face; using '%s'\n",
                set->name, set->defaultFace, set->faces[0].name);
    }
    return &set->faces[0];
}

// Looks up a symbol name in a .codepoints file (lines of "name hexcodepoint"),
// returning its codepoint or 0 if not found.
static uint32_t lookupCodepointByName(const char *codepointsPath, const char *name) {
    FILE *fp = fopen(codepointsPath, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot open %s\n", codepointsPath);
        return 0;
    }
    size_t nameLen = strlen(name);
    char line[256];
    uint32_t codepoint = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, name, nameLen) == 0 && line[nameLen] == ' ') {
            codepoint = (uint32_t)strtoul(line + nameLen + 1, NULL, 16);
            break;
        }
    }
    fclose(fp);
    return codepoint;
}

// True for a --font= argument that names a file rather than an installed font,
// so the same by-path loader serves both spellings.
static int looksLikeFontPath(const char *arg) {
    if (strchr(arg, '/') != NULL) return 1;
    const char *dot = strrchr(arg, '.');
    if (dot == NULL) return 0;
    if (strcasecmp(dot, ".ttf") != 0 && strcasecmp(dot, ".otf") != 0 &&
        strcasecmp(dot, ".ttc") != 0 && strcasecmp(dot, ".otc") != 0) {
        return 0;
    }
    return isRegularFile(arg);
}

static void printUsage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  SF Symbols mode: %s <name> <weight> <size> [--output=<path>]\n", prog);
    fprintf(stderr, "  Weights: black, bold, heavy, light, medium, regular, semibold, thin, ultralight\n");
    fprintf(stderr, "  Example: %s heart bold 768 --output=/path/to/file.svg\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "  Custom font mode: %s --font=<name|path> <characters|codepoint> <size> [--output=<path>]\n", prog);
    fprintf(stderr, "  Example: %s --font=Helvetica \"Hello\" 100 --output=/path/to/file.svg\n", prog);
    fprintf(stderr, "  Example: %s --font=Helvetica U+1F600 100 --output=/path/to/file.svg\n", prog);
    fprintf(stderr, "  Codepoint format: U+XXXX or 0xXXXX\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  Font by path: %s --font-file=<path> --codepoints=<path> <name> [<weight>] <size>\n", prog);
    fprintf(stderr, "                %s --font-file=<path> <characters|codepoint> <size>\n", prog);
    fprintf(stderr, "  With --codepoints=<path> the first argument is a symbol name looked up in that file.\n");
    fprintf(stderr, "  Example: %s --font-file=./mdi.ttf --codepoints=./mdi.codepoints home 256 --output=home.svg\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "  Glyph set mode: %s --set=<name|dir> [--face=<face>] <name> [<weight>] <size> [--output=<path>]\n", prog);
    fprintf(stderr, "  A set is a directory of icon fonts described by a %s manifest.\n", GLYPHSET_MANIFEST);
    fprintf(stderr, "  Give it as a path, or as a bare name to look up under $GLYPHSVG_SET_PATH\n");
    fprintf(stderr, "  and then the current directory. Nowhere else is searched.\n");
    fprintf(stderr, "  Example: %s --set=./fonts/mdi home 256 --output=home.svg\n", prog);
    fprintf(stderr, "  Example: GLYPHSVG_SET_PATH=./fonts %s --set=mdi home 256 --output=home.svg\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "  Material Symbols mode: %s --material[=<style>] <name> [<weight>] <size> [--weight=<N>] [--fill[=<0..1>]] [--output=<path>]\n", prog);
    fprintf(stderr, "  Alias for --set=material --face=<style>. Styles: outlined (default), rounded, sharp\n");
    fprintf(stderr, "  Example: %s --material favorite 256 --fill --output=favorite.svg\n", prog);
    fprintf(stderr, "  Run ./material/download.sh once to fetch the fonts and codepoints,\n");
    fprintf(stderr, "  then point GLYPHSVG_MATERIAL_DIR at that directory (or run from beside it).\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  Shared options for --set / --material / --font-file:\n");
    fprintf(stderr, "    --weight=<N|name>  wght variation axis, or the nearest face of a static family\n");
    fprintf(stderr, "    --fill[=<0..1>]    FILL variation axis; default is outline\n");
    fprintf(stderr, "    --axis=<TAG>=<N>   any other variation axis, e.g. --axis=GRAD=200\n");
    fprintf(stderr, "    --info             print the resolved paths, faces and variation axes, then exit\n");
    fprintf(stderr, "  Variation axes a font does not declare are ignored, so static fonts work unchanged.\n");
    fprintf(stderr, "  --face=<name> (alias --style=) selects a face of a --set / --material set.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  --version   Print the version and exit\n");
}

static uint32_t parseCodepoint(const char *str) {
    if (str == NULL) return 0;
    size_t len = strlen(str);
    if (len < 4) return 0;

    const char *hexStart = str;
    if (str[0] == 'U' && str[1] == '+') {
        hexStart = str + 2;
    } else if (str[0] == '0' && str[1] == 'x') {
        hexStart = str + 2;
    } else {
        return 0;
    }

    char *endptr;
    unsigned long val = strtoul(hexStart, &endptr, 16);
    if (*endptr != '\0') return 0;
    return (uint32_t)val;
}

typedef enum {
    MODE_SF_SYMBOLS,     // built-in name map + installed SF Pro Text
    MODE_INSTALLED_FONT, // --font=<name>
    MODE_FONT_FILE,      // --font-file=<path>
    MODE_GLYPH_SET       // --set=<name|dir>, --material
} RenderMode;

int main(int argc, const char *argv[]) {
    const char *customFont = NULL;
    const char *fontFileArg = NULL;
    const char *codepointsArg = NULL;
    const char *setArg = NULL;
    const char *faceArg = NULL;
    const char *output = NULL;
    const char *weightFlag = NULL;
    const char *fillFlag = NULL;
    const char *materialStyleArg = NULL;
    int materialFlag = 0;
    int infoMode = 0;

    AxisRequest axisRequests[MAX_AXIS_REQUESTS];
    int nAxisRequests = 0;

    const char *positionals[8];
    int nPos = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("%s\n", GLYPHSVG_VERSION);
            return 0;
        }
    }

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--font=", 7) == 0) {
            customFont = argv[i] + 7;
        } else if (strncmp(argv[i], "--font-file=", 12) == 0) {
            fontFileArg = argv[i] + 12;
        } else if (strncmp(argv[i], "--codepoints=", 13) == 0) {
            codepointsArg = argv[i] + 13;
        } else if (strncmp(argv[i], "--set=", 6) == 0) {
            setArg = argv[i] + 6;
        } else if (strncmp(argv[i], "--face=", 7) == 0) {
            faceArg = argv[i] + 7;
        } else if (strncmp(argv[i], "--style=", 8) == 0) {
            faceArg = argv[i] + 8;
        } else if (strncmp(argv[i], "--output=", 9) == 0) {
            output = argv[i] + 9;
        } else if (strncmp(argv[i], "--material=", 11) == 0) {
            materialFlag = 1;
            materialStyleArg = argv[i] + 11;
        } else if (strcmp(argv[i], "--material") == 0) {
            materialFlag = 1;
        } else if (strncmp(argv[i], "--weight=", 9) == 0) {
            weightFlag = argv[i] + 9;
        } else if (strncmp(argv[i], "--fill=", 7) == 0) {
            fillFlag = argv[i] + 7;
        } else if (strcmp(argv[i], "--fill") == 0) {
            fillFlag = ""; // bare --fill => fully filled
        } else if (strncmp(argv[i], "--axis=", 7) == 0) {
            const char *spec = argv[i] + 7;
            const char *eq = strchr(spec, '=');
            size_t tagLen = (eq != NULL) ? (size_t)(eq - spec) : 0;
            if (eq == NULL || tagLen == 0 || tagLen > 4) {
                fprintf(stderr, "Error: --axis expects <TAG>=<value>, e.g. --axis=GRAD=200\n");
                return 1;
            }
            if (nAxisRequests >= MAX_USER_AXIS_REQUESTS) {
                fprintf(stderr, "Error: at most %d --axis options are supported\n", MAX_USER_AXIS_REQUESTS);
                return 1;
            }
            char tagStr[8] = {0};
            memcpy(tagStr, spec, tagLen);
            uint32_t tag = axisTagFromString(tagStr);
            char *end;
            double value = strtod(eq + 1, &end);
            if (tag == 0 || end == eq + 1 || *end != '\0' || isnan(value)) {
                fprintf(stderr, "Error: invalid --axis value '%s'\n", spec);
                return 1;
            }
            addAxisRequest(axisRequests, &nAxisRequests, tag, value, 1);
        } else if (strcmp(argv[i], "--info") == 0) {
            infoMode = 1;
        } else if (nPos < (int)(sizeof(positionals) / sizeof(positionals[0]))) {
            positionals[nPos++] = argv[i];
        } else {
            fprintf(stderr, "Too many arguments\n");
            printUsage(argv[0]);
            return 1;
        }
    }

    // --material[=<style>] is an alias for --set=material [--face=<style>]. An
    // explicit --face wins over the style spelling; naming a different set at
    // the same time is a contradiction rather than a last-flag-wins.
    if (materialFlag) {
        if (setArg != NULL) {
            fprintf(stderr, "Error: --set and --material cannot be combined\n");
            return 1;
        }
        setArg = "material";
        if (faceArg == NULL) faceArg = materialStyleArg;
    }

    // A --font= argument naming a file is routed to the by-path loader, so it
    // gains the same variation-axis handling as --font-file=.
    if (customFont != NULL && fontFileArg == NULL && looksLikeFontPath(customFont)) {
        fontFileArg = customFont;
        customFont = NULL;
    }

    int nSources = (customFont != NULL) + (fontFileArg != NULL) + (setArg != NULL);
    if (nSources > 1) {
        fprintf(stderr, "Error: --font, --font-file and --set/--material are mutually exclusive\n");
        return 1;
    }

    RenderMode mode = MODE_SF_SYMBOLS;
    if (setArg != NULL) {
        mode = MODE_GLYPH_SET;
    } else if (fontFileArg != NULL) {
        mode = MODE_FONT_FILE;
    } else if (customFont != NULL) {
        mode = MODE_INSTALLED_FONT;
    }

    if (codepointsArg != NULL && mode != MODE_FONT_FILE) {
        fprintf(stderr, "Error: --codepoints only applies to --font-file (a set names its own codepoints)\n");
        return 1;
    }
    if (infoMode && mode != MODE_GLYPH_SET && mode != MODE_FONT_FILE) {
        fprintf(stderr, "Error: --info requires --set, --material or --font-file\n");
        return 1;
    }

    // Names are looked up in a .codepoints table; the other modes take characters
    // or a codepoint directly.
    int useNameLookup = (mode == MODE_GLYPH_SET) ||
                        (mode == MODE_FONT_FILE && codepointsArg != NULL);

    const char *name = NULL;
    const char *weightName = NULL;
    const char *charInput = NULL;
    double size = 0;

    if (infoMode) {
        // --info reports on the font, not on a glyph: positionals are optional.
        // A single one names a weight, so the reported face is the one a render
        // at that weight would use.
        if (nPos == 1) {
            weightName = positionals[0];
        } else if (nPos > 1) {
            fprintf(stderr, "Warning: --info takes at most one argument (a weight); ignoring %d others\n", nPos - 1);
            weightName = positionals[0];
        }
    } else if (mode == MODE_SF_SYMBOLS) {
        // <name> <weight> <size>
        if (nPos != 3) {
            printUsage(argv[0]);
            return 1;
        }
        name = positionals[0];
        weightName = positionals[1];
        size = strtod(positionals[2], NULL);
    } else if (useNameLookup) {
        // <name> [<weight>] <size>
        if (nPos == 3) {
            name = positionals[0];
            weightName = positionals[1];
            size = strtod(positionals[2], NULL);
        } else if (nPos == 2) {
            name = positionals[0];
            size = strtod(positionals[1], NULL);
        } else {
            printUsage(argv[0]);
            return 1;
        }
    } else {
        // <characters|codepoint> <size>
        if (nPos < 2) {
            fprintf(stderr, "Usage: %s --font=<name|path> <characters|codepoint> <size> [--output=<path>]\n", argv[0]);
            fprintf(stderr, "Example: %s --font=Helvetica \"Hello\" 100 --output=/path/to/file.svg\n", argv[0]);
            return 1;
        }
        charInput = positionals[0];
        size = strtod(positionals[1], NULL);
    }

    // Resolve the font file and its codepoints table. --set finds them through a
    // manifest, --font-file is given them outright; both feed the same loader.
    char ttfPath[1024] = {0};
    char cpPath[1024] = {0};
    char metadataPath[1024] = {0};
    GlyphSet set;
    const GlyphFace *face = NULL;
    int weightConsumedByFace = 0;

    // The weight may arrive as --weight= or positionally, named or numeric. For
    // the by-path modes it is resolved once here: face selection and the wght
    // axis both read it. SF Symbols mode picks its font by weight name instead.
    const char *weightSpec = (weightFlag != NULL && weightFlag[0] != '\0') ? weightFlag : weightName;
    double weightValue = 400.0;
    if (mode == MODE_GLYPH_SET || mode == MODE_FONT_FILE) {
        weightValue = resolveWeightValue(weightFlag, weightName);
    }

    if (mode == MODE_GLYPH_SET) {
        if (!resolveGlyphSet(setArg, faceArg, weightSpec, weightValue, &set)) {
            fprintf(stderr, "Error: Glyph set '%s' not found (no readable font in a matching directory).\n", setArg);
            if (strchr(setArg, '/') != NULL) {
                fprintf(stderr, "       A set directory needs a %s manifest naming a font file that exists.\n", GLYPHSET_MANIFEST);
            } else if (strcasecmp(setArg, "material") == 0) {
                fprintf(stderr, "       Run ./material/download.sh to fetch it, or set GLYPHSVG_MATERIAL_DIR.\n");
            } else {
                fprintf(stderr, "       Looked for <root>/%s/%s under $GLYPHSVG_SET_PATH and the\n", setArg, GLYPHSET_MANIFEST);
                fprintf(stderr, "       current directory. Pass --set=<dir> to name the directory outright.\n");
            }
            return 1;
        }
        face = resolveFace(&set, faceArg, weightSpec, weightValue, &weightConsumedByFace);
        if (face == NULL) {
            fprintf(stderr, "Error: Unknown face '%s' in set '%s'\n", faceArg, set.name);
            fprintf(stderr, "Available faces: ");
            printFaceList(stderr, &set);
            return 1;
        }
        snprintf(ttfPath, sizeof(ttfPath), "%s/%s", set.dir, face->fontFile);
        if (face->codepointsFile[0] != '\0') {
            snprintf(cpPath, sizeof(cpPath), "%s/%s", set.dir, face->codepointsFile);
        }
        if (set.metadataFile[0] != '\0') {
            snprintf(metadataPath, sizeof(metadataPath), "%s/%s", set.dir, set.metadataFile);
        }
    } else if (mode == MODE_FONT_FILE) {
        if (!isRegularFile(fontFileArg)) {
            fprintf(stderr, "Error: No font file at %s\n", fontFileArg);
            return 1;
        }
        if (faceArg != NULL) {
            fprintf(stderr, "Warning: --face applies to --set and --material; one font file has one face\n");
        }
        snprintf(ttfPath, sizeof(ttfPath), "%s", fontFileArg);
        if (codepointsArg != NULL) {
            snprintf(cpPath, sizeof(cpPath), "%s", codepointsArg);
        }
    }

    if (infoMode) {
        if (mode == MODE_GLYPH_SET) {
            printf("set: %s\n", set.name);
            if (set.title[0] != '\0') printf("title: %s\n", set.title);
            if (set.license[0] != '\0') printf("license: %s\n", set.license);
            if (set.kind[0] != '\0') printf("kind: %s\n", set.kind);
            printf("dir: %s\n", set.dir);
            printf("faces: ");
            printFaceList(stdout, &set);
            printf("face: %s\n", face->name);
        }
        printf("font: %s\n", ttfPath);
        if (cpPath[0] != '\0') printf("codepoints: %s\n", cpPath);
        if (metadataPath[0] != '\0' && isRegularFile(metadataPath)) printf("metadata: %s\n", metadataPath);

        CTFontRef infoFont = createFontFromPath(ttfPath, NULL, 0, NULL);
        if (infoFont == NULL) return 1;
        char psName[256] = {0};
        CFStringRef psNameRef = CTFontCopyPostScriptName(infoFont);
        if (psNameRef != NULL) {
            CFStringGetCString(psNameRef, psName, sizeof(psName), kCFStringEncodingUTF8);
            CFRelease(psNameRef);
        }
        if (psName[0] != '\0') printf("postscript-name: %s\n", psName);
        printf("glyphs: %ld\n", (long)CTFontGetGlyphCount(infoFont));
        // Printed before the axis lines so a parser can stop reading at "variable: no".
        CFArrayRef axes = CTFontCopyVariationAxes(infoFont);
        CFIndex nAxes = (axes != NULL) ? CFArrayGetCount(axes) : 0;
        printf("variable: %s\n", (nAxes > 0) ? "yes" : "no");
        printAxisLines(axes);
        if (axes != NULL) CFRelease(axes);
        CFRelease(infoFont);
        return 0;
    }

    if (size <= 0) {
        fprintf(stderr, "Error: invalid or missing size\n");
        printUsage(argv[0]);
        return 1;
    }

    // Weight and fill become variation axis requests. A static font declares
    // neither axis and ignores them; a variable font honors them whether or not a
    // face was also picked, so a face answering the weight only silences the
    // warning - it never discards the value the user asked for.
    if (mode == MODE_GLYPH_SET || mode == MODE_FONT_FILE) {
        int weightRequested = (weightSpec != NULL && weightSpec[0] != '\0');
        addAxisRequest(axisRequests, &nAxisRequests, AXIS_TAG_WGHT, weightValue, weightRequested);
        if (weightConsumedByFace) {
            markAxisAnsweredByFace(axisRequests, nAxisRequests, AXIS_TAG_WGHT);
        }
        addAxisRequest(axisRequests, &nAxisRequests, AXIS_TAG_FILL,
                       resolveFillValue(fillFlag), fillFlag != NULL);
    } else if (weightFlag != NULL || fillFlag != NULL || faceArg != NULL || nAxisRequests > 0) {
        fprintf(stderr, "Warning: --face, --weight, --fill and --axis apply to --set, --material and --font-file only\n");
    }

    uint32_t codepoint = 0;
    int isCodepointInput = 0;

    if (useNameLookup) {
        if (cpPath[0] == '\0') {
            fprintf(stderr, "Error: No codepoints file for '%s'; pass --codepoints=<path>\n",
                    (mode == MODE_GLYPH_SET) ? face->name : ttfPath);
            return 1;
        }
        codepoint = lookupCodepointByName(cpPath, name);
        if (codepoint == 0) {
            fprintf(stderr, "Error: Unknown symbol '%s' in %s\n", name, cpPath);
            return 1;
        }
        if (mode == MODE_GLYPH_SET) {
            fprintf(stderr, "Codepoint: 0x%lX  set: %s  face: %s\n",
                    (unsigned long)codepoint, set.name, face->name);
        } else {
            fprintf(stderr, "Codepoint: 0x%lX\n", (unsigned long)codepoint);
        }
    } else if (mode == MODE_SF_SYMBOLS) {
        loadMappingsDict();
        codepoint = getCodepointForName(name);
        if (codepoint == 0) {
            fprintf(stderr, "Error: Unknown symbol '%s'\n", name);
            return 1;
        }
        fprintf(stderr, "Codepoint: 0x%lX\n", (unsigned long)codepoint);
    } else {
        codepoint = parseCodepoint(charInput);
        if (codepoint > 0) {
            isCodepointInput = 1;
            fprintf(stderr, "Codepoint: 0x%lX\n", (unsigned long)codepoint);
        }
    }

    CTFontRef font;
    if (mode == MODE_GLYPH_SET || mode == MODE_FONT_FILE) {
        font = createFontFromPath(ttfPath, axisRequests, nAxisRequests, NULL);
    } else {
        font = createFont(customFont, weightName);
    }
    if (font == NULL) {
        return 1;
    }
    fprintf(stderr, "Font loaded\n");

    int numChars = 1;
    const char *charString = NULL;

    if (charInput != NULL && !isCodepointInput) {
        charString = charInput;
        numChars = countChars(charInput);
    }

    // Per-glyph outcomes, so the exit status reflects what was actually written.
    // A glyph the font does not contain (missing) is an error. A glyph that exists
    // but has no outline (blank) is normal for whitespace inside a multi-character
    // string, so it only matters when it leaves us with nothing to write at all.
    int emitted = 0;
    int missing = 0;
    int blank = 0;
    int failed = 0;

    if (numChars > 1 && output != NULL) {
        struct stat ost;
        size_t olen = strlen(output);
        int outIsDir = (olen > 0 && output[olen - 1] == '/') ||
                       (stat(output, &ost) == 0 && S_ISDIR(ost.st_mode));
        if (!outIsDir) {
            fprintf(stderr, "Error: --output must be a directory when extracting %d glyphs; "
                            "'%s' is a single file\n", numChars, output);
            CFRelease(font);
            return 1;
        }
    }

    for (int charIdx = 0; charIdx < numChars; charIdx++) {
        uint32_t cp = codepoint;
        char charLabel[32] = {0};

        if (charString != NULL) {
            getCharLabel(charString, charIdx, &cp, charLabel, sizeof(charLabel));
        }

        // CTFontGetGlyphsForCharacters writes one glyph per UTF-16 unit, so a
        // surrogate pair needs a two-element buffer even though only the lead
        // unit carries the glyph.
        CGGlyph glyphs[2] = {0, 0};
        if (cp > 0xFFFF) {
            uint32_t temp = cp - 0x10000;
            UniChar chars[2] = {
                0xD800 + (temp >> 10),
                0xDC00 + (temp & 0x3FF)
            };
            CTFontGetGlyphsForCharacters(font, chars, glyphs, 2);
        } else {
            UniChar chars[1] = {cp};
            CTFontGetGlyphsForCharacters(font, chars, glyphs, 1);
        }
        CGGlyph glyph = glyphs[0];

        if (glyph == 0) {
            fprintf(stderr, "Error: No glyph for codepoint 0x%lX in this font\n", (unsigned long)cp);
            missing++;
            continue;
        }

        CGAffineTransform transform = CGAffineTransformIdentity;
        CGPathRef path = CTFontCreatePathForGlyph(font, glyph, &transform);
        if (path == NULL) {
            // No outline: whitespace and other blank glyphs land here.
            fprintf(stderr, "Warning: Glyph for codepoint 0x%lX has no outline, skipping\n", (unsigned long)cp);
            blank++;
            continue;
        }

        CGRect bounds = CGPathGetBoundingBox(path);
        if (bounds.size.width <= 0 || bounds.size.height <= 0) {
            fprintf(stderr, "Warning: Empty glyph bounds for codepoint 0x%lX, skipping\n", (unsigned long)cp);
            blank++;
            CGPathRelease(path);
            continue;
        }

        double scale = fmin(size / bounds.size.width, size / bounds.size.height);
        double newWidth = bounds.size.width * scale;
        double newHeight = bounds.size.height * scale;
        double offsetX = (size - newWidth) / 2.0;
        double offsetY = (size - newHeight) / 2.0;

        CGAffineTransform finalTransform = {
            .a = scale, .b = 0,
            .c = 0,     .d = -scale,
            .tx = -bounds.origin.x * scale + offsetX,
            .ty = (bounds.origin.y + bounds.size.height) * scale + offsetY
        };

        CGPathRef scaledPath = CGPathCreateCopyByTransformingPath(path, &finalTransform);
        CGPathRelease(path);
        if (scaledPath == NULL) {
            fprintf(stderr, "Error: Failed to transform path\n");
            CFRelease(font);
            return 1;
        }
        CGRect scaledBounds = CGPathGetBoundingBox(scaledPath);

        int truncated = 0;
        char *svgd = pathToSVG(scaledPath, &truncated);
        CGPathRelease(scaledPath);
        if (truncated) {
            fprintf(stderr, "Error: Refusing to write truncated SVG for codepoint 0x%lX\n", (unsigned long)cp);
            failed++;
            continue;
        }

        int printToStdout = (output == NULL && numChars == 1);

        // Does --output point to a directory (trailing slash or existing dir)?
        int outputIsDir = 0;
        if (output != NULL) {
            struct stat ost;
            size_t olen = strlen(output);
            if ((olen > 0 && output[olen - 1] == '/') ||
                (stat(output, &ost) == 0 && S_ISDIR(ost.st_mode))) {
                outputIsDir = 1;
            }
        }

        if (output != NULL && !outputIsDir && numChars == 1) {
            FILE *fp = fopen(output, "w");
            if (fp == NULL) {
                fprintf(stderr, "Error: Cannot open file %s for writing\n", output);
                CFRelease(font);
                return 1;
            }
            writeSVG(fp, size, scaledBounds, svgd);
            fclose(fp);
            emitted++;
            printf("SVG saved to %s\n", output);
        } else if (printToStdout) {
            writeSVG(stdout, size, scaledBounds, svgd);
            emitted++;
        } else {
            if (output == NULL) {
                output = "./";
            }
            struct stat st;
            int is_dir = 0;
            size_t output_len = strlen(output);
            if (output_len > 0 && output[output_len - 1] == '/') {
                is_dir = 1;
            } else if (stat(output, &st) == 0 && S_ISDIR(st.st_mode)) {
                is_dir = 1;
            }

            char filename[1024];
            if (is_dir) {
                char dir_path[1024];
                if (output_len > 0 && output[output_len - 1] != '/') {
                    snprintf(dir_path, sizeof(dir_path), "%s/", output);
                } else {
                    strncpy(dir_path, output, sizeof(dir_path) - 1);
                    dir_path[sizeof(dir_path) - 1] = '\0';
                }
                if (charString != NULL) {
                    snprintf(filename, sizeof(filename), "%s%s_%d.svg", dir_path, charLabel, charIdx);
                } else if (useNameLookup) {
                    snprintf(filename, sizeof(filename), "%s%s.svg", dir_path, name);
                } else {
                    snprintf(filename, sizeof(filename), "%sU+%04lX.svg", dir_path, (unsigned long)codepoint);
                }
            } else {
                strncpy(filename, output, sizeof(filename) - 1);
                filename[sizeof(filename) - 1] = '\0';
            }

            FILE *fp = fopen(filename, "w");
            if (fp == NULL) {
                fprintf(stderr, "Error: Cannot open file %s for writing\n", filename);
                CFRelease(font);
                return 1;
            }
            writeSVG(fp, size, scaledBounds, svgd);
            fclose(fp);
            emitted++;

            printf("SVG saved to %s\n", filename);
        }
    }

    CFRelease(font);

    if (emitted == 0) {
        fprintf(stderr, "Error: No SVG output produced from %d glyph(s): %d missing, %d blank, %d failed\n",
                numChars, missing, blank, failed);
        return 1;
    }
    if (missing > 0 || failed > 0) {
        fprintf(stderr, "Error: %d of %d glyph(s) could not be extracted "
                        "(%d missing, %d blank, %d failed); %d written\n",
                missing + failed, numChars, missing, blank, failed, emitted);
        return 1;
    }

    return 0;
}
