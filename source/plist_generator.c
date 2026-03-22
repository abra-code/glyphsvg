#import <CoreFoundation/CoreFoundation.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>

int main(int argc, const char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <names.txt> <symbols.txt> <output.plist>\n", argv[0]);
        return 1;
    }
    const char *namesFile = argv[1];
    const char *symbolsFile = argv[2];
    const char *outputFile = argv[3];
    
    FILE *fp = fopen(namesFile, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot open %s\n", namesFile);
        return 1;
    }
    
    char line[256];
    char **names = NULL;
    size_t namesCount = 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        char **tmp = realloc(names, sizeof(char *) * (namesCount + 1));
        if (tmp == NULL) {
            fprintf(stderr, "Error: Out of memory\n");
            return 1;
        }
        names = tmp;
        names[namesCount] = strdup(line);
        namesCount++;
    }
    fclose(fp);
    
    fp = fopen(symbolsFile, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot open %s\n", symbolsFile);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    size_t symbolsSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *symbolsData = malloc(symbolsSize);
    if (symbolsData == NULL) {
        fprintf(stderr, "Error: Out of memory\n");
        return 1;
    }
    if (fread(symbolsData, 1, symbolsSize, fp) != symbolsSize) {
        fprintf(stderr, "Error: Failed to read %s\n", symbolsFile);
        return 1;
    }
    fclose(fp);
    
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, namesCount, &kCFCopyStringDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    
    for (size_t i = 0; i < namesCount && i * 4 < symbolsSize; i++) {
        unsigned char b1 = symbolsData[i * 4];
        unsigned char b2 = symbolsData[i * 4 + 1];
        unsigned char b3 = symbolsData[i * 4 + 2];
        unsigned char b4 = symbolsData[i * 4 + 3];
        uint32_t codepoint = ((b1 & 0x07) << 18) | ((b2 & 0x3f) << 12) | ((b3 & 0x3f) << 6) | (b4 & 0x3f);
        
        CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault, names[i], kCFStringEncodingUTF8);
        CFNumberRef value = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &codepoint);
        CFDictionarySetValue(dict, key, value);
        CFRelease(key);
        CFRelease(value);
    }
    
    CFErrorRef error = NULL;
    CFDataRef plistData = CFPropertyListCreateData(kCFAllocatorDefault, dict, kCFPropertyListBinaryFormat_v1_0, 0, &error);
    
    if (plistData == NULL) {
        fprintf(stderr, "Error: Failed to create binary plist\n");
        return 1;
    }
    
    fp = fopen(outputFile, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot open %s for writing\n", outputFile);
        return 1;
    }
    
    fwrite(CFDataGetBytePtr(plistData), 1, CFDataGetLength(plistData), fp);
    fclose(fp);
    
    return 0;
}
