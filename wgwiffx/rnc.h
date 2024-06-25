#pragma once
#include <memory>

int decompress_rnc(unsigned char* compressedBuff, int compressedSize, std::unique_ptr<unsigned char[]>* rawBuff, int* rawSize);