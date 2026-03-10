/* EPANET 3
 *
 * Copyright (c) 2016 Open Water Analytics
 * Licensed under the terms of the MIT License (see the LICENSE file for details).
 *
 */

//! \file jsoninputreader.h
//! \brief Describes the JsonInputReader class.

#ifndef JSONINPUTREADER_H_
#define JSONINPUTREADER_H_

#include <istream>
#include <string>

class Network;

//! \class JsonInputReader
//! \brief Reads EPANET network data stored in JSON directly into a network.
//!
//! The JSON reader parses a section-oriented JSON document and applies each
//! section directly to the network using the existing object/property parsers.
//! This preserves the current parsing logic without converting the full JSON
//! document into a synthetic INP text file first.

class JsonInputReader
{
  public:

    static bool isJsonInput(const char* fileName, std::istream& stream);
    static void readFile(std::istream& stream, Network* network);
};

#endif
