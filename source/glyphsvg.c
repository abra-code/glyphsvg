#import <CoreText/CoreText.h>
#import <CoreFoundation/CoreFoundation.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>
#import <math.h>
#import <sys/stat.h>
#import <mach-o/dyld.h>

static CFDictionaryRef mappingsDict = NULL;

static void loadMappingsDict(void) {
    char exePath[1024];
    uint32_t exePathSize = sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &exePathSize) != 0) {
        return;
    }
    char *slash = strrchr(exePath, '/');
    if (slash == NULL) {
        return;
    }
    *slash = '\0';
    char plistPath[1024];
    snprintf(plistPath, sizeof(plistPath), "%s/sfmap.plist", exePath);
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
    const char *name = NULL;
    const char *weight = NULL;
    const char *customFont = NULL;
    const char *charInput = NULL;
    double size = 0;
    const char *output = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--font=", 7) == 0) {
            customFont = argv[i] + 7;
        } else if (strncmp(argv[i], "--output=", 9) == 0) {
            output = argv[i] + 9;
        } else if (name == NULL) {
            name = argv[i];
        } else if (weight == NULL && customFont == NULL) {
            weight = argv[i];
        } else if (size == 0) {
            size = strtod(argv[i], NULL);
        } else {
            fprintf(stderr, "Too many arguments\n");
            printUsage(argv[0]);
            return 1;
        }
    }
    
    int isCustomFontMode = (customFont != NULL);
    
    if (isCustomFontMode) {
        if (name == NULL || size <= 0) {
            fprintf(stderr, "Usage: %s --font=<name> <characters|codepoint> <size> [--output=<path>]\n", argv[0]);
            fprintf(stderr, "Example: %s --font=Helvetica \"Hello\" 100 --output=/path/to/file.svg\n", argv[0]);
            return 1;
        }
        charInput = name;
    } else {
        if (name == NULL || weight == NULL || size <= 0) {
            printUsage(argv[0]);
            return 1;
        }
    }
    
    loadMappingsDict();
    
    uint32_t codepoint = 0;
    int isCodepointInput = 0;
    
    if (isCustomFontMode) {
        codepoint = parseCodepoint(charInput);
        if (codepoint > 0) {
            isCodepointInput = 1;
            fprintf(stderr, "Codepoint: 0x%lX\n", (unsigned long)codepoint);
        }
    } else {
        codepoint = getCodepointForName(name);
        if (codepoint == 0) {
            fprintf(stderr, "Error: Unknown symbol '%s'\n", name);
            return 1;
        }
        fprintf(stderr, "Codepoint: 0x%lX\n", (unsigned long)codepoint);
    }
    
    CTFontRef font = createFont(customFont, weight);
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
        
        if (output != NULL && numChars == 1) {
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
