/* EPANET 3
 *
 * Copyright (c) 2016 Open Water Analytics
 * Licensed under the terms of the MIT License (see the LICENSE file for details).
 *
 */

//! \file jsonprojectwriter.h
//! \brief Describes the JsonProjectWriter class.

#ifndef JSONPROJECTWRITER_H_
#define JSONPROJECTWRITER_H_

#include <fstream>

class Network;

//! \class JsonProjectWriter
//! \brief Writes a project's data to a JSON file.

class JsonProjectWriter
{
  public:
    JsonProjectWriter();
    ~JsonProjectWriter();
    int writeFile(const char* fname, Network* nw);
};

#endif
