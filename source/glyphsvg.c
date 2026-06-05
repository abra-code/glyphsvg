#import <CoreText/CoreText.h>
#import <CoreFoundation/CoreFoundation.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>
#import <math.h>
#import <sys/stat.h>
#import <mach-o/dyld.h>

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

static CTFontRef createFont(const char *customFont, const char *weight) {
    CTFontRef font = NULL;
    if (customFont != NULL) {
        CFStringRef fontNameCF = CFStringCreateWithCString(NULL, customFont, kCFStringEncodingUTF8);
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
        font = CTFontCreateWithName(fontNameCF, 12.0, NULL);
        CFRelease(fontNameCF);
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
        *offset = ctx->bufferSize;
    } else {
        *offset += n;
        if (*offset >= bufferSize) {
            *offset = bufferSize - 1; // Leave room for null terminator
        }
    }
}

// Convert CGPath to SVG path string
static char *pathToSVG(CGPathRef path) {
    // Use a static buffer for simplicity (not thread-safe, but acceptable for CLI tool)
    static char svgBuffer[50000];
    size_t svgOffset = 0;
    PathToSVGContext context = {svgBuffer, &svgOffset, sizeof(svgBuffer)};
    
    CGPathApply(path, &context, pathElementApplier);
    
    // Null-terminate
    if (svgOffset >= sizeof(svgBuffer) - 1) {
        svgBuffer[sizeof(svgBuffer)-1] = '\0';
        fprintf(stderr, "Warning: SVG path data truncated (exceeded %zu bytes)\n", sizeof(svgBuffer));
    } else {
        svgBuffer[svgOffset] = '\0';
    }

    return svgBuffer;
}

// ----- Google Material Symbols support -----

static const char *materialStyles[] = { "Outlined", "Rounded", "Sharp", NULL };

// Returns the canonical capitalized style name, or NULL if invalid.
// A NULL or empty input defaults to "Outlined".
static const char *resolveMaterialStyle(const char *style) {
    if (style == NULL || style[0] == '\0') {
        return materialStyles[0];
    }
    for (int i = 0; materialStyles[i] != NULL; i++) {
        if (strcasecmp(style, materialStyles[i]) == 0) {
            return materialStyles[i];
        }
    }
    return NULL;
}

typedef struct { const char *name; double value; } WeightName;

// SF Symbols-style weight names mapped to approximate numeric font weights
// (Apple SF Pro numerics). Material's wght axis only spans 100-700, so heavy
// and black are clamped down at use sites.
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

// Material Symbols wght axis range.
#define MATERIAL_WEIGHT_MIN 100.0
#define MATERIAL_WEIGHT_MAX 700.0

static double clampMaterialWeight(double w) {
    if (w < MATERIAL_WEIGHT_MIN) return MATERIAL_WEIGHT_MIN;
    if (w > MATERIAL_WEIGHT_MAX) return MATERIAL_WEIGHT_MAX;
    return w;
}

// Resolves the numeric weight for material mode from the explicit --weight=
// flag (numeric or named) and/or a positional named weight, defaulting to
// regular (400). The result is clamped to the Material wght axis range.
static double resolveMaterialWeight(const char *weightFlag, const char *weightName) {
    if (weightFlag != NULL && weightFlag[0] != '\0') {
        char *end;
        double v = strtod(weightFlag, &end);
        if (*end == '\0') {
            return clampMaterialWeight(v);
        }
        double nv = weightNameToValue(weightFlag);
        if (nv > 0) return clampMaterialWeight(nv);
        fprintf(stderr, "Warning: unknown --weight '%s', using regular (400)\n", weightFlag);
        return 400.0;
    }
    if (weightName != NULL && weightName[0] != '\0') {
        double nv = weightNameToValue(weightName);
        if (nv > 0) return clampMaterialWeight(nv);
        fprintf(stderr, "Warning: unknown weight '%s', using regular (400)\n", weightName);
        return 400.0;
    }
    return 400.0;
}

static int isRegularFile(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Locates the material data directory for the given canonical style, filling in
// the codepoints and ttf paths. Searches an env override, locations relative to
// the executable, then the current directory. Returns 1 on success.
static int findMaterialFiles(const char *style, char *cpOut, size_t cpSize, char *ttfOut, size_t ttfSize) {
    char exeDir[1024] = {0};
    getExeDir(exeDir, sizeof(exeDir));

    char candidates[8][1024];
    int n = 0;
    const char *env = getenv("GLYPHSVG_MATERIAL_DIR");
    if (env != NULL && env[0] != '\0') {
        snprintf(candidates[n++], sizeof(candidates[0]), "%s", env);
    }
    if (exeDir[0] != '\0') {
        snprintf(candidates[n++], sizeof(candidates[0]), "%s/material", exeDir);
        snprintf(candidates[n++], sizeof(candidates[0]), "%s/../material", exeDir);
        snprintf(candidates[n++], sizeof(candidates[0]), "%s/../../material", exeDir);
    }
    snprintf(candidates[n++], sizeof(candidates[0]), "./material");

    for (int i = 0; i < n; i++) {
        char cp[1024];
        snprintf(cp, sizeof(cp), "%s/MaterialSymbols%s.codepoints", candidates[i], style);
        if (isRegularFile(cp)) {
            snprintf(cpOut, cpSize, "%s", cp);
            snprintf(ttfOut, ttfSize, "%s/MaterialSymbols%s.ttf", candidates[i], style);
            return 1;
        }
    }
    return 0;
}

// Looks up a symbol name in a Material .codepoints file (lines of
// "name hexcodepoint"), returning its codepoint or 0 if not found.
static uint32_t getMaterialCodepoint(const char *codepointsPath, const char *name) {
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

// Loads a Material Symbols variable font from a file, applying the requested
// wght axis value to the returned instance.
static CTFontRef createMaterialFont(const char *ttfPath, double weight) {
    CFStringRef pathStr = CFStringCreateWithCString(NULL, ttfPath, kCFStringEncodingUTF8);
    if (pathStr == NULL) return NULL;
    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, pathStr, kCFURLPOSIXPathStyle, false);
    CFRelease(pathStr);
    if (url == NULL) return NULL;

    CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromURL(url);
    CFRelease(url);
    if (descriptors == NULL || CFArrayGetCount(descriptors) == 0) {
        if (descriptors != NULL) CFRelease(descriptors);
        fprintf(stderr, "Error: Could not load font from %s\n", ttfPath);
        return NULL;
    }
    CTFontDescriptorRef baseDesc = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descriptors, 0);

    // Build a variation dictionary for the wght axis (four-char code 'wght').
    int32_t wghtTag = 0x77676874; // 'wght'
    CFNumberRef axisKey = CFNumberCreate(NULL, kCFNumberSInt32Type, &wghtTag);
    CFNumberRef axisVal = CFNumberCreate(NULL, kCFNumberDoubleType, &weight);
    CFMutableDictionaryRef variation = CFDictionaryCreateMutable(NULL, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(variation, axisKey, axisVal);
    CFRelease(axisKey);
    CFRelease(axisVal);

    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(NULL, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontVariationAttribute, variation);
    CFRelease(variation);

    CTFontDescriptorRef varDesc = CTFontDescriptorCreateCopyWithAttributes(baseDesc, attrs);
    CFRelease(attrs);
    CFRelease(descriptors);
    if (varDesc == NULL) {
        fprintf(stderr, "Error: Could not apply variation to font\n");
        return NULL;
    }

    CTFontRef font = CTFontCreateWithFontDescriptor(varDesc, 12.0, NULL);
    CFRelease(varDesc);
    return font;
}

static void printUsage(const char *prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  SF Symbols mode: %s <name> <weight> <size> [--output=<path>]\n", prog);
    fprintf(stderr, "  Weights: black, bold, heavy, light, medium, regular, semibold, thin, ultralight\n");
    fprintf(stderr, "  Example: %s heart bold 768 --output=/path/to/file.svg\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "  Custom font mode: %s --font=<name> <characters|codepoint> <size> [--output=<path>]\n", prog);
    fprintf(stderr, "  Example: %s --font=Helvetica \"Hello\" 100 --output=/path/to/file.svg\n", prog);
    fprintf(stderr, "  Example: %s --font=Helvetica U+1F600 100 --output=/path/to/file.svg\n", prog);
    fprintf(stderr, "  Codepoint format: U+XXXX or 0xXXXX\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  Material Symbols mode: %s --material[=<style>] <name> [<weight>] <size> [--weight=<N>] [--output=<path>]\n", prog);
    fprintf(stderr, "  Styles: outlined (default), rounded, sharp\n");
    fprintf(stderr, "  Weight: black, bold, heavy, light, medium, regular, semibold, thin, ultralight or numeric 100-700 via --weight=<N>\n");
    fprintf(stderr, "  Example: %s --material=rounded settings bold 256 --output=settings.svg\n", prog);
    fprintf(stderr, "  Run ./material/download.sh once to fetch the fonts and codepoints.\n");
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

int main(int argc, const char *argv[]) {
    const char *customFont = NULL;
    const char *output = NULL;
    int materialMode = 0;
    const char *materialStyleArg = NULL;
    const char *weightFlag = NULL;

    const char *positionals[8];
    int nPos = 0;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--font=", 7) == 0) {
            customFont = argv[i] + 7;
        } else if (strncmp(argv[i], "--output=", 9) == 0) {
            output = argv[i] + 9;
        } else if (strncmp(argv[i], "--material=", 11) == 0) {
            materialMode = 1;
            materialStyleArg = argv[i] + 11;
        } else if (strcmp(argv[i], "--material") == 0) {
            materialMode = 1;
        } else if (strncmp(argv[i], "--weight=", 9) == 0) {
            weightFlag = argv[i] + 9;
        } else if (nPos < (int)(sizeof(positionals) / sizeof(positionals[0]))) {
            positionals[nPos++] = argv[i];
        } else {
            fprintf(stderr, "Too many arguments\n");
            printUsage(argv[0]);
            return 1;
        }
    }

    if (customFont != NULL && materialMode) {
        fprintf(stderr, "Error: --font and --material cannot be combined\n");
        return 1;
    }

    const char *name = NULL;
    const char *weightName = NULL;
    const char *charInput = NULL;
    double size = 0;

    int isCustomFontMode = (customFont != NULL);

    if (isCustomFontMode) {
        if (nPos < 2) {
            fprintf(stderr, "Usage: %s --font=<name> <characters|codepoint> <size> [--output=<path>]\n", argv[0]);
            fprintf(stderr, "Example: %s --font=Helvetica \"Hello\" 100 --output=/path/to/file.svg\n", argv[0]);
            return 1;
        }
        charInput = positionals[0];
        size = strtod(positionals[1], NULL);
    } else if (materialMode) {
        // --material[=style] <name> [<weight>] <size>
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
        // SF Symbols: <name> <weight> <size>
        if (nPos != 3) {
            printUsage(argv[0]);
            return 1;
        }
        name = positionals[0];
        weightName = positionals[1];
        size = strtod(positionals[2], NULL);
    }

    if (size <= 0) {
        fprintf(stderr, "Error: invalid or missing size\n");
        printUsage(argv[0]);
        return 1;
    }

    uint32_t codepoint = 0;
    int isCodepointInput = 0;
    char ttfPath[1024] = {0};
    double materialWeight = 400.0;

    if (isCustomFontMode) {
        codepoint = parseCodepoint(charInput);
        if (codepoint > 0) {
            isCodepointInput = 1;
            fprintf(stderr, "Codepoint: 0x%lX\n", (unsigned long)codepoint);
        }
    } else if (materialMode) {
        const char *style = resolveMaterialStyle(materialStyleArg);
        if (style == NULL) {
            fprintf(stderr, "Error: Unknown material style '%s'\n", materialStyleArg);
            fprintf(stderr, "Valid styles: outlined, rounded, sharp\n");
            return 1;
        }
        char cpPath[1024];
        if (!findMaterialFiles(style, cpPath, sizeof(cpPath), ttfPath, sizeof(ttfPath))) {
            fprintf(stderr, "Error: Material data not found. Run ./material/download.sh to fetch it,\n");
            fprintf(stderr, "       or set GLYPHSVG_MATERIAL_DIR to the directory containing the files.\n");
            return 1;
        }
        codepoint = getMaterialCodepoint(cpPath, name);
        if (codepoint == 0) {
            fprintf(stderr, "Error: Unknown material symbol '%s'\n", name);
            return 1;
        }
        materialWeight = resolveMaterialWeight(weightFlag, weightName);
        fprintf(stderr, "Codepoint: 0x%lX  style: %s  weight: %.0f\n",
                (unsigned long)codepoint, style, materialWeight);
    } else {
        loadMappingsDict();
        codepoint = getCodepointForName(name);
        if (codepoint == 0) {
            fprintf(stderr, "Error: Unknown symbol '%s'\n", name);
            return 1;
        }
        fprintf(stderr, "Codepoint: 0x%lX\n", (unsigned long)codepoint);
    }

    CTFontRef font;
    if (materialMode) {
        font = createMaterialFont(ttfPath, materialWeight);
    } else {
        font = createFont(customFont, weightName);
    }
    if (font == NULL) {
        return 1;
    }
    fprintf(stderr, "Font loaded\n");
    
    int numChars = 1;
    const char *charString = NULL;
    
    if (isCustomFontMode && !isCodepointInput) {
        charString = charInput;
        numChars = countChars(charInput);
    }
    
    for (int charIdx = 0; charIdx < numChars; charIdx++) {
        uint32_t cp = codepoint;
        char charLabel[32] = {0};
        
        if (charString != NULL) {
            getCharLabel(charString, charIdx, &cp, charLabel, sizeof(charLabel));
        }
        
        CGGlyph glyph;
        if (cp > 0xFFFF) {
            uint32_t temp = cp - 0x10000;
            UniChar chars[2] = {
                0xD800 + (temp >> 10),
                0xDC00 + (temp & 0x3FF)
            };
            CTFontGetGlyphsForCharacters(font, chars, &glyph, 2);
        } else {
            UniChar chars[1] = {cp};
            CTFontGetGlyphsForCharacters(font, chars, &glyph, 1);
        }
        
        if (glyph == 0) {
            fprintf(stderr, "Warning: No glyph for codepoint 0x%lX, skipping\n", (unsigned long)cp);
            continue;
        }
        
        CGAffineTransform transform = CGAffineTransformIdentity;
        CGPathRef path = CTFontCreatePathForGlyph(font, glyph, &transform);
        if (path == NULL) {
            fprintf(stderr, "Warning: Failed to create path for codepoint 0x%lX, skipping\n", (unsigned long)cp);
            continue;
        }
        
        CGRect bounds = CGPathGetBoundingBox(path);
        if (bounds.size.width <= 0 || bounds.size.height <= 0) {
            fprintf(stderr, "Warning: Invalid glyph bounds for %s, skipping\n", charLabel);
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
        
        char *svgd = pathToSVG(scaledPath);
        CGPathRelease(scaledPath);
        
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
            printf("SVG saved to %s\n", output);
        } else if (printToStdout) {
            writeSVG(stdout, size, scaledBounds, svgd);
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
                if (isCustomFontMode) {
                    snprintf(filename, sizeof(filename), "%s%s_%d.svg", dir_path, charLabel, charIdx);
                } else if (materialMode) {
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
            
            printf("SVG saved to %s\n", filename);
        }
    }
    
    CFRelease(font);
    
    return 0;
}
