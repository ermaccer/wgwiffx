// wgwiffx.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <vector>
#include <fstream>
#include <memory>
#include <string>
#include <filesystem>
#include <algorithm>

#include "image.h"
#include "rnc.h"

// PC
// decomp function at 0x424C40
// handler at 0x424D30
// 
// rnc code https://github.com/lab313ru/rnc_propack_source

struct WIFF_BLOCK {
    int  header;
    int  dataSize;
};


// COL4
// 0 - paletteID
// 4 - numColors
// 8 - short colors[numColors]



struct PaletteEntry {
    int paletteID;
    std::vector<rgbr_pal_entry> colors;
};

// DOT4
// 0 - paletteID
// 4 - short y
// 6 - short x
// PC:
// RNC data block
// ARCADE:
// raw data

#pragma pack(push,1)
struct DOT4_header {
    int paletteID;
    short y;
    short x;
};
#pragma pack(pop)


void changeEndINT(int* value)
{
    *value = (*value & 0x000000FFU) << 24 | (*value & 0x0000FF00U) << 8 | (*value & 0x00FF0000U) >> 8 | (*value & 0xFF000000U) >> 24;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
        return 0;

    bool isArcade = false;

    std::string inputFile = argv[argc - 1];

    std::ifstream pFile(inputFile, std::ifstream::binary);

    if (!pFile)
    {
        std::cout << "ERROR: Could not open " << inputFile << "!" << std::endl;;
        return 1;
    }

    std::string extension = inputFile.substr(inputFile.find_last_of(".") + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    if (extension == "img")
        isArcade = true;

    unsigned int header = 0;
    pFile.read((char*)&header, sizeof(unsigned int));

    if (!(header == 'FFIW'))
    {
        std::cout << "ERROR: "<< inputFile << "is not a valid WarGods .W file!" << std::endl;;
        return 1;
    }

    int unk = 0;
    pFile.read((char*)&unk, sizeof(unsigned int));
    changeEndINT(&unk);

    std::cout << "INFO: Scanning data..."<< std::endl;

    // only need DOT4 (pixels) and COL4 colors blocks for image extraction

    std::vector<unsigned int> dotOffsets;
    std::vector<unsigned int> colOffsets;

    while (!pFile.eof())
    {
        int blockName = 0;
        pFile.read((char*)&blockName, sizeof(int));

        unsigned int currentOffset = pFile.tellg();
        currentOffset -= sizeof(int);

        if (blockName == '4TOD')
        {
            dotOffsets.push_back(currentOffset);
            std::cout << "INFO: Found image data at " << currentOffset << std::endl;
        }

        if (blockName == '4LOC')
        {
            colOffsets.push_back(currentOffset);
            std::cout << "INFO: Found palette data at " << currentOffset << std::endl;
        }
    }
    
    pFile.clear();
    pFile.seekg(0, pFile.beg);

    std::cout << "INFO: Scanning finished." << std::endl;
    std::cout << "INFO: " << colOffsets.size() << " Palettes" << std::endl;
    std::cout << "INFO: " << dotOffsets.size() << " Images" << std::endl;


    if (colOffsets.size() == 0 || dotOffsets.size() == 0)
    {
        std::cout << "INFO: This WIFF has no palettes or images." << std::endl;
        return 1;
    }

    std::vector<PaletteEntry> palettes;

    // obtain all palettes

    for (unsigned int i = 0; i < colOffsets.size(); i++)
    {
        int palOffset = colOffsets[i];
        pFile.seekg(palOffset, pFile.beg);

        WIFF_BLOCK palBlock;
        pFile.read((char*)&palBlock, sizeof(WIFF_BLOCK));
        changeEndINT(&palBlock.dataSize);


        PaletteEntry pal;
        pFile.read((char*)&pal.paletteID, sizeof(int));

        int numColors = 0;
        pFile.read((char*)&numColors, sizeof(int));

        // safe guard for arcade garbage scan data

        if (numColors > 4096)
            numColors = 4096;

        for (int a = 0; a < numColors; a++)
        {
            short m_sPixel;
            pFile.read((char*)&m_sPixel, sizeof(short));

            rgbr_pal_entry color = {};
            color.r = 8 * (m_sPixel & 31);	m_sPixel >>= 5;
            color.g = 8 * (m_sPixel & 31);	m_sPixel >>= 5;
            color.b = 8 * (m_sPixel & 31);

            rgbr_pal_entry cr = color;
            pal.colors.push_back(cr);
        }
        std::cout << "Palette " << pal.paletteID << " colors " << numColors << " offset " << palOffset << std::endl;
        palettes.push_back(pal);

    }

    for (unsigned int i = 0; i < palettes.size(); i++)
    {
        std::cout << "INFO: Pal " << palettes[i].paletteID << " Colors: " << palettes[i].colors.size() << std::endl;
    }

    std::string input = inputFile;
    std::string folder = input.substr(0, input.find_last_of("."));


    std::filesystem::create_directory(folder);
    std::filesystem::current_path(folder);


    // extract images pc/arcade

    int imageCounter = 0;

    for (unsigned int i = 0; i < dotOffsets.size(); i++)
    {
        int imageOffset = dotOffsets[i];
         
        std::cout << "INFO: Image at " << imageOffset << std::endl;

        pFile.seekg(imageOffset, pFile.beg);

        WIFF_BLOCK imageBlock;
        pFile.read((char*)&imageBlock, sizeof(WIFF_BLOCK));
        changeEndINT(&imageBlock.dataSize);
        DOT4_header dot4;
        pFile.read((char*)&dot4, sizeof(DOT4_header));

        std::cout << "ID: " << dot4.paletteID << " " << dot4.x << " x " << dot4.y << std::endl;

        {
            int dataSize = imageBlock.dataSize - sizeof(DOT4_header);

            std::unique_ptr<unsigned char[]> rawBuff;
            int rawSize = 0;
            int result = 0;

            bool arcadeHack = false;

            //  hack to fix arcade scanner
            if (dot4.x == 0 || dot4.y == 0 || dot4.paletteID > 600 || dot4.x > 4096 || dot4.y > 4096)
                arcadeHack = true;

            if (!arcadeHack)
            {
                if (!isArcade)
                {
                    std::unique_ptr<unsigned char[]> dataBuff = std::make_unique<unsigned char[]>(dataSize);
                    pFile.read((char*)dataBuff.get(), dataSize);

                    int result = decompress_rnc(dataBuff.get(), dataSize, &rawBuff, &rawSize);
                }
                else
                {
                    rawSize = imageBlock.dataSize - 8;
                    if (rawSize > 0)
                    {
                        rawBuff = std::make_unique<unsigned char[]>(rawSize);
                        pFile.read((char*)rawBuff.get(), rawSize);
                    }

                }

                if (result == 0 && rawSize > 0)
                {
                    int width = (dot4.x + 3) & ~3;
                    int height = dot4.y;


                    // palettes that share same ID, another hack for arcade
                    std::vector<int> palIDs;

                    for (unsigned int a = 0; a < palettes.size(); a++)
                    {
                        if (palettes[a].paletteID == dot4.paletteID)
                        {
                            palIDs.push_back(a);
                        }
                    }

                    if (palIDs.size() > 0)
                    {
                        for (int b = 0; b < palIDs.size(); b++)
                        {
                            std::string output = std::to_string(imageCounter);
                            output += "_";
                            output += std::to_string(i);
                            output += "_";
                            output += std::to_string(dot4.paletteID);
                            output += ".bmp";

                            PaletteEntry pal = palettes[palIDs[b]];

                            bmp_header bmp;
                            bmp_info_header bmpf;
                            bmp.bfType = 'MB';
                            bmp.bfSize = width * height;
                            bmp.bfReserved1 = 0;
                            bmp.bfReserved2 = 0;
                            bmp.bfOffBits = sizeof(bmp_header) + sizeof(bmp_info_header) + (sizeof(rgbr_pal_entry) * pal.colors.size());
                            bmpf.biSize = sizeof(bmp_info_header);
                            bmpf.biWidth = width;
                            bmpf.biHeight = height;
                            bmpf.biPlanes = 1;
                            bmpf.biBitCount = 8;
                            bmpf.biCompression = 0;
                            bmpf.biXPelsPerMeter = 0;
                            bmpf.biYPelsPerMeter = 0;
                            bmpf.biClrUsed = pal.colors.size();
                            bmpf.biClrImportant = 0;

                            std::ofstream oFile(output, std::ofstream::binary);

                            oFile.write((char*)&bmp, sizeof(bmp_header));
                            oFile.write((char*)&bmpf, sizeof(bmp_info_header));

                            for (int i = 0; i < pal.colors.size(); i++)
                            {
                                oFile.write((char*)&pal.colors[i], sizeof(rgbr_pal_entry));
                            }


                            int imgSize = width * height;
                            std::unique_ptr<unsigned char[]> newBuff = std::make_unique<unsigned char[]>(imgSize);
                            memcpy(newBuff.get(), rawBuff.get(), imgSize);



                            for (int y = height - 1; y >= 0; y--)
                            {
                                for (int x = 0; x < width; x++)
                                {
                                    oFile.write((char*)&newBuff[x + (y * width)], sizeof(char));
                                }
                            }
                            oFile.close();
                            std::cout << output << " saved! Iteration " << b << std::endl;
                            imageCounter++;
                        }
                      
                    }
                    else
                    {
                        std::cout << "ERROR: Could not find palette " << dot4.paletteID << "!" << std::endl;
                    }
                }
            }
        }
    }

    return 0;
}

