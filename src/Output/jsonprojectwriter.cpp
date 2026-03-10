/* EPANET 3
 *
 * Copyright (c) 2016 Open Water Analytics
 * Licensed under the terms of the MIT License (see the LICENSE file for details).
 *
 */

#include "jsonprojectwriter.h"
#include "Core/network.h"
#include "Core/error.h"
#include "Core/constants.h"
#include "Elements/control.h"
#include "Elements/curve.h"
#include "Elements/demand.h"
#include "Elements/emitter.h"
#include "Elements/junction.h"
#include "Elements/pipe.h"
#include "Elements/pump.h"
#include "Elements/pattern.h"
#include "Elements/qualsource.h"
#include "Elements/reservoir.h"
#include "Elements/tank.h"
#include "Elements/valve.h"
#include "Models/tankmixmodel.h"
#include "Utilities/utilities.h"

#include <cmath>
#include <iomanip>
#include <sstream>

using namespace std;

namespace
{

string jsonEscape(const string& text)
{
    ostringstream out;
    for (char ch : text)
    {
        switch (ch)
        {
        case '\\': out << "\\\\"; break;
        case '"':  out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
            {
                out << "\\u" << hex << setw(4) << setfill('0')
                    << static_cast<int>(static_cast<unsigned char>(ch))
                    << dec << setfill(' ');
            }
            else out << ch;
        }
    }
    return out.str();
}

string quoted(const string& text)
{
    return string("\"") + jsonEscape(text) + "\"";
}

string numberToString(double value)
{
    ostringstream out;
    out << setprecision(15) << value;
    return out.str();
}

void indent(ostream& out, int n)
{
    for (int i = 0; i < n; i++) out << ' ';
}

void writeKey(ostream& out, int n, const string& key)
{
    indent(out, n);
    out << quoted(key) << ": ";
}

void writeStringArray(ostream& out, const vector<string>& items, int n)
{
    out << "[\n";
    for (size_t i = 0; i < items.size(); i++)
    {
        indent(out, n + 2);
        out << quoted(items[i]);
        if ( i + 1 < items.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

string qualityTypeName(Network* network)
{
    switch (network->option(Options::QUAL_TYPE))
    {
    case Options::NOQUAL: return "NONE";
    case Options::AGE: return "AGE";
    case Options::TRACE: return "TRACE";
    case Options::CHEM: return "CHEMICAL";
    }
    return "NONE";
}

string flowUnitsName(int flowUnits)
{
    switch (flowUnits)
    {
    case 0:  return "CFS";
    case 1:  return "GPM";
    case 2:  return "MGD";
    case 3:  return "IMGD";
    case 4:  return "AFD";
    case 5:  return "LPS";
    case 6:  return "LPM";
    case 7:  return "MLD";
    case 8:  return "CMH";
    case 9:  return "CMD";
    default: return "GPM";
    }
}

string pressureUnitsName(int pressureUnits)
{
    switch (pressureUnits)
    {
    case 0:  return "PSI";
    case 1:  return "METERS";
    case 2:  return "PKA";
    default: return "PSI";
    }
}

string statisticTypeName(int statistic)
{
    switch (statistic)
    {
    case 1: return "AVERAGE";
    case 2: return "MINIMUM";
    case 3: return "MAXIMUM";
    case 4: return "RANGE";
    default: return "SERIES";
    }
}

void writeTitleSection(ostream& out, Network* network, int n, bool& first)
{
    if ( network->title.empty() ) return;
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "title");
    writeStringArray(out, network->title, n);
}

void writeJunctions(ostream& out, Network* network, int n, bool& first)
{
    vector<Junction*> junctions;
    for (Node* node : network->nodes)
    {
        if ( node->type() == Node::JUNCTION ) junctions.push_back(static_cast<Junction*>(node));
    }
    if ( junctions.empty() ) return;

    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "junctions");
    out << "[\n";
    for (size_t i = 0; i < junctions.size(); i++)
    {
        Junction* junc = junctions[i];
        indent(out, n + 2);
        out << "{\n";
        writeKey(out, n + 4, "id"); out << quoted(junc->name) << ",\n";
        writeKey(out, n + 4, "elevation"); out << numberToString(junc->elev * network->ucf(Units::LENGTH));
        if ( junc->primaryDemand.baseDemand != 0.0 || junc->primaryDemand.timePattern )
        {
            out << ",\n";
            writeKey(out, n + 4, "demand");
            out << numberToString(junc->primaryDemand.baseDemand * network->ucf(Units::FLOW));
            if ( junc->primaryDemand.timePattern )
            {
                out << ",\n";
                writeKey(out, n + 4, "pattern"); out << quoted(junc->primaryDemand.timePattern->name);
            }
        }
        indent(out, n + 2);
        out << '}';
        if ( i + 1 < junctions.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeReservoirs(ostream& out, Network* network, int n, bool& first)
{
    vector<Reservoir*> reservoirs;
    for (Node* node : network->nodes)
    {
        if ( node->type() == Node::RESERVOIR ) reservoirs.push_back(static_cast<Reservoir*>(node));
    }
    if ( reservoirs.empty() ) return;

    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "reservoirs");
    out << "[\n";
    for (size_t i = 0; i < reservoirs.size(); i++)
    {
        Reservoir* resv = reservoirs[i];
        indent(out, n + 2);
        out << "{\n";
        writeKey(out, n + 4, "id"); out << quoted(resv->name) << ",\n";
        writeKey(out, n + 4, "head"); out << numberToString(resv->elev * network->ucf(Units::LENGTH));
        if ( resv->headPattern )
        {
            out << ",\n";
            writeKey(out, n + 4, "pattern"); out << quoted(resv->headPattern->name);
        }
        indent(out, n + 2);
        out << '}';
        if ( i + 1 < reservoirs.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeTanks(ostream& out, Network* network, int n, bool& first)
{
    vector<Tank*> tanks;
    for (Node* node : network->nodes)
    {
        if ( node->type() == Node::TANK ) tanks.push_back(static_cast<Tank*>(node));
    }
    if ( tanks.empty() ) return;

    if ( !first ) out << ",\n";
    first = false;
    double ucfLength = network->ucf(Units::LENGTH);
    writeKey(out, n, "tanks");
    out << "[\n";
    for (size_t i = 0; i < tanks.size(); i++)
    {
        Tank* tank = tanks[i];
        indent(out, n + 2);
        out << "{\n";
        writeKey(out, n + 4, "id"); out << quoted(tank->name) << ",\n";
        writeKey(out, n + 4, "elevation"); out << numberToString(tank->elev * ucfLength) << ",\n";
        writeKey(out, n + 4, "initialLevel"); out << numberToString((tank->initHead - tank->elev) * ucfLength) << ",\n";
        writeKey(out, n + 4, "minimumLevel"); out << numberToString((tank->minHead - tank->elev) * ucfLength) << ",\n";
        writeKey(out, n + 4, "maximumLevel"); out << numberToString((tank->maxHead - tank->elev) * ucfLength) << ",\n";
        writeKey(out, n + 4, "diameter"); out << numberToString(tank->diameter * ucfLength) << ",\n";
        writeKey(out, n + 4, "minimumVolume"); out << numberToString(tank->minVolume * network->ucf(Units::VOLUME));
        if ( tank->volCurve )
        {
            out << ",\n";
            writeKey(out, n + 4, "volumeCurve"); out << quoted(tank->volCurve->name);
        }
        indent(out, n + 2);
        out << '}';
        if ( i + 1 < tanks.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writePipes(ostream& out, Network* network, int n, bool& first)
{
    vector<Pipe*> pipes;
    for (Link* link : network->links)
    {
        if ( link->type() == Link::PIPE ) pipes.push_back(static_cast<Pipe*>(link));
    }
    if ( pipes.empty() ) return;

    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "pipes");
    out << "[\n";
    for (size_t i = 0; i < pipes.size(); i++)
    {
        Pipe* pipe = pipes[i];
        double roughness = pipe->roughness;
        if ( network->option(Options::HEADLOSS_MODEL) == "D-W" )
        {
            roughness = roughness * network->ucf(Units::LENGTH) * 1000.0;
        }
        indent(out, n + 2);
        out << "{\n";
        writeKey(out, n + 4, "id"); out << quoted(pipe->name) << ",\n";
        writeKey(out, n + 4, "from"); out << quoted(pipe->fromNode->name) << ",\n";
        writeKey(out, n + 4, "to"); out << quoted(pipe->toNode->name) << ",\n";
        writeKey(out, n + 4, "length"); out << numberToString(pipe->length * network->ucf(Units::LENGTH)) << ",\n";
        writeKey(out, n + 4, "diameter"); out << numberToString(pipe->diameter * network->ucf(Units::DIAMETER)) << ",\n";
        writeKey(out, n + 4, "roughness"); out << numberToString(roughness) << ",\n";
        writeKey(out, n + 4, "minorLoss"); out << numberToString(pipe->lossCoeff);
        if ( pipe->hasCheckValve )
        {
            out << ",\n";
            writeKey(out, n + 4, "status"); out << quoted("CV");
        }
        else if ( pipe->initStatus == Link::LINK_CLOSED )
        {
            out << ",\n";
            writeKey(out, n + 4, "status"); out << quoted("CLOSED");
        }
        indent(out, n + 2);
        out << '}';
        if ( i + 1 < pipes.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writePumps(ostream& out, Network* network, int n, bool& first)
{
    vector<Pump*> pumps;
    for (Link* link : network->links)
    {
        if ( link->type() == Link::PUMP ) pumps.push_back(static_cast<Pump*>(link));
    }
    if ( pumps.empty() ) return;

    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "pumps");
    out << "[\n";
    for (size_t i = 0; i < pumps.size(); i++)
    {
        Pump* pump = pumps[i];
        indent(out, n + 2);
        out << "{\n";
        writeKey(out, n + 4, "id"); out << quoted(pump->name) << ",\n";
        writeKey(out, n + 4, "from"); out << quoted(pump->fromNode->name) << ",\n";
        writeKey(out, n + 4, "to"); out << quoted(pump->toNode->name);
        bool hasMore = false;
        if ( pump->pumpCurve.horsepower > 0.0 )
        {
            out << ",\n";
            writeKey(out, n + 4, "power"); out << numberToString(pump->pumpCurve.horsepower * network->ucf(Units::POWER));
            hasMore = true;
        }
        if ( pump->pumpCurve.curveType != PumpCurve::NO_CURVE )
        {
            if ( !hasMore ) out << ",\n";
            else out << ",\n";
            writeKey(out, n + 4, "head"); out << quoted(pump->pumpCurve.curve->name);
            hasMore = true;
        }
        if ( pump->speed > 0.0 && pump->speed != 1.0 )
        {
            out << ",\n";
            writeKey(out, n + 4, "speed"); out << numberToString(pump->speed);
            hasMore = true;
        }
        if ( pump->speedPattern )
        {
            out << ",\n";
            writeKey(out, n + 4, "pattern"); out << quoted(pump->speedPattern->name);
        }
        indent(out, n + 2);
        out << '}';
        if ( i + 1 < pumps.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeValves(ostream& out, Network* network, int n, bool& first)
{
    vector<Valve*> valves;
    for (Link* link : network->links)
    {
        if ( link->type() == Link::VALVE ) valves.push_back(static_cast<Valve*>(link));
    }
    if ( valves.empty() ) return;

    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "valves");
    out << "[\n";
    for (size_t i = 0; i < valves.size(); i++)
    {
        Valve* valve = valves[i];
        indent(out, n + 2);
        out << "{\n";
        writeKey(out, n + 4, "id"); out << quoted(valve->name) << ",\n";
        writeKey(out, n + 4, "from"); out << quoted(valve->fromNode->name) << ",\n";
        writeKey(out, n + 4, "to"); out << quoted(valve->toNode->name) << ",\n";
        writeKey(out, n + 4, "diameter"); out << numberToString(valve->diameter * network->ucf(Units::DIAMETER)) << ",\n";
        writeKey(out, n + 4, "type"); out << quoted(Valve::ValveTypeWords[(int)valve->valveType]) << ",\n";
        writeKey(out, n + 4, "setting");
        if (valve->valveType == Valve::GPV)
        {
            out << quoted(network->curve((int)valve->initSetting)->name);
        }
        else
        {
            double cf = valve->initSetting / valve->convertSetting(network, valve->initSetting);
            out << numberToString(cf * valve->initSetting);
        }
        indent(out, n + 2);
        out << '}';
        if ( i + 1 < valves.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeDemands(ostream& out, Network* network, int n, bool& first)
{
    vector<string> items;
    for (Node* node : network->nodes)
    {
        if ( node->type() != Node::JUNCTION ) continue;
        Junction* junc = static_cast<Junction*>(node);
        for (list<Demand>::iterator it = junc->demands.begin(); it != junc->demands.end(); ++it)
        {
            ostringstream item;
            item << "{\"node\":" << quoted(node->name)
                 << ",\"base\":" << numberToString(it->baseDemand * network->ucf(Units::FLOW));
            if ( it->timePattern ) item << ",\"pattern\":" << quoted(it->timePattern->name);
            item << '}';
            items.push_back(item.str());
        }
    }
    if ( items.empty() ) return;

    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "demands");
    out << "[\n";
    for (size_t i = 0; i < items.size(); i++)
    {
        indent(out, n + 2);
        out << items[i];
        if ( i + 1 < items.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeEmitters(ostream& out, Network* network, int n, bool& first)
{
    vector<string> items;
    for (Node* node : network->nodes)
    {
        if ( node->type() != Node::JUNCTION ) continue;
        Junction* junc = static_cast<Junction*>(node);
        Emitter* emitter = junc->emitter;
        if ( !emitter ) continue;

        double qUcf = network->ucf(Units::FLOW);
        double pUcf = network->ucf(Units::PRESSURE);
        ostringstream item;
        item << "{\"node\":" << quoted(node->name)
             << ",\"coefficient\":" << numberToString(emitter->flowCoeff * qUcf * pow(pUcf, emitter->expon))
             << ",\"exponent\":" << numberToString(emitter->expon);
        if ( emitter->timePattern ) item << ",\"pattern\":" << quoted(emitter->timePattern->name);
        item << '}';
        items.push_back(item.str());
    }
    if ( items.empty() ) return;

    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "emitters");
    out << "[\n";
    for (size_t i = 0; i < items.size(); i++)
    {
        indent(out, n + 2);
        out << items[i];
        if ( i + 1 < items.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeStatus(ostream& out, Network* network, int n, bool& first)
{
    vector<string> items;
    for (Link* link : network->links)
    {
        if ( link->type() == Link::PUMP )
        {
            if ( link->initSetting == 0 || link->initStatus == Link::LINK_CLOSED )
            {
                ostringstream item;
                item << "{\"link\":" << quoted(link->name)
                     << ",\"status\":\"CLOSED\"}";
                items.push_back(item.str());
            }
        }
        else if ( link->type() == Link::VALVE )
        {
            if ( link->initStatus == Link::LINK_OPEN || link->initStatus == Link::LINK_CLOSED )
            {
                ostringstream item;
                item << "{\"link\":" << quoted(link->name)
                     << ",\"status\":"
                     << quoted(link->initStatus == Link::LINK_OPEN ? "OPEN" : "CLOSED")
                     << '}';
                items.push_back(item.str());
            }
        }
    }
    if ( items.empty() ) return;
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "status");
    out << "[\n";
    for (size_t i = 0; i < items.size(); i++)
    {
        indent(out, n + 2);
        out << items[i];
        if ( i + 1 < items.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeLeakage(ostream& out, Network* network, int n, bool& first)
{
    vector<string> items;
    for (Link* link : network->links)
    {
        if ( link->type() != Link::PIPE ) continue;
        Pipe* pipe = static_cast<Pipe*>(link);
        if ( pipe->leakCoeff1 <= 0.0 ) continue;

        ostringstream item;
        item << "{\"link\":" << quoted(link->name)
             << ",\"coeff1\":" << numberToString(pipe->leakCoeff1)
             << ",\"coeff2\":" << numberToString(pipe->leakCoeff2) << '}';
        items.push_back(item.str());
    }
    if ( items.empty() ) return;
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "leakage");
    out << "[\n";
    for (size_t i = 0; i < items.size(); i++)
    {
        indent(out, n + 2);
        out << items[i];
        if ( i + 1 < items.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writePatterns(ostream& out, Network* network, int n, bool& first)
{
    if ( network->patterns.empty() ) return;
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "patterns");
    out << "[\n";
    for (size_t i = 0; i < network->patterns.size(); i++)
    {
        Pattern* pattern = network->patterns[i];
        indent(out, n + 2);
        out << "{\n";
        writeKey(out, n + 4, "id"); out << quoted(pattern->name) << ",\n";
        writeKey(out, n + 4, "type"); out << quoted(pattern->type == Pattern::FIXED_PATTERN ? "FIXED" : "VARIABLE");
        if ( pattern->type == Pattern::FIXED_PATTERN )
        {
            if ( pattern->timeInterval() > 0 )
            {
                out << ",\n";
                writeKey(out, n + 4, "interval"); out << quoted(Utilities::getTime(pattern->timeInterval()));
            }
            out << ",\n";
            writeKey(out, n + 4, "factors");
            out << '[';
            for (int j = 0; j < pattern->size(); j++)
            {
                if ( j > 0 ) out << ", ";
                out << numberToString(pattern->factor(j));
            }
            out << ']';
        }
        else
        {
            VariablePattern* varPat = static_cast<VariablePattern*>(pattern);
            out << ",\n";
            writeKey(out, n + 4, "points");
            out << "[\n";
            for (int j = 0; j < pattern->size(); j++)
            {
                indent(out, n + 6);
                out << "{\"time\":" << quoted(Utilities::getTime(varPat->time(j)))
                    << ",\"factor\":" << numberToString(pattern->factor(j)) << '}';
                if ( j + 1 < pattern->size() ) out << ',';
                out << "\n";
            }
            indent(out, n + 4);
            out << ']';
        }
        out << "\n";
        indent(out, n + 2);
        out << '}';
        if ( i + 1 < network->patterns.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeCurves(ostream& out, Network* network, int n, bool& first)
{
    if ( network->curves.empty() ) return;
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "curves");
    out << "[\n";
    for (size_t i = 0; i < network->curves.size(); i++)
    {
        Curve* curve = network->curves[i];
        indent(out, n + 2);
        out << "{\n";
        writeKey(out, n + 4, "id"); out << quoted(curve->name);
        if ( curve->curveType() != Curve::UNKNOWN )
        {
            out << ",\n";
            writeKey(out, n + 4, "type"); out << quoted(Curve::CurveTypeWords[curve->curveType()]);
        }
        out << ",\n";
        writeKey(out, n + 4, "points");
        out << "[\n";
        for (int j = 0; j < curve->size(); j++)
        {
            indent(out, n + 6);
            out << '{';
            out << "\"x\":" << numberToString(curve->x(j)) << ",\"y\":" << numberToString(curve->y(j));
            out << '}';
            if ( j + 1 < curve->size() ) out << ',';
            out << "\n";
        }
        indent(out, n + 4);
        out << "]\n";
        indent(out, n + 2);
        out << '}';
        if ( i + 1 < network->curves.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeControls(ostream& out, Network* network, int n, bool& first)
{
    if ( network->controls.empty() ) return;
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "controls");
    out << "[\n";
    for (size_t i = 0; i < network->controls.size(); i++)
    {
        indent(out, n + 2);
        out << quoted(network->controls[i]->toStr(network));
        if ( i + 1 < network->controls.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeEnergy(ostream& out, Network* network, int n, bool& first)
{
    bool hasSection = false;
    for (Link* link : network->links)
    {
        if ( link->type() != Link::PUMP ) continue;
        Pump* pump = static_cast<Pump*>(link);
        if ( pump->efficCurve || pump->costPattern || pump->costPerKwh > 0.0 )
        {
            hasSection = true;
            break;
        }
    }
    if ( network->option(Options::ENERGY_PRICE) != 0.0 ||
         network->option(Options::PEAKING_CHARGE) != 0.0 ||
         network->option(Options::PUMP_EFFICIENCY) != 0.0 ||
         network->option(Options::ENERGY_PRICE_PATTERN) >= 0 )
    {
        hasSection = true;
    }
    if ( !hasSection ) return;

    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "energy");
    out << "{\n";

    bool wroteAny = false;
    if ( network->option(Options::ENERGY_PRICE) != 0.0 ||
         network->option(Options::PUMP_EFFICIENCY) != 0.0 ||
         network->option(Options::ENERGY_PRICE_PATTERN) >= 0 )
    {
        writeKey(out, n + 2, "global");
        out << "{\n";
        bool firstGlobal = true;
        if ( network->option(Options::ENERGY_PRICE) != 0.0 )
        {
            if ( !firstGlobal ) out << ",\n";
            firstGlobal = false;
            writeKey(out, n + 4, "price"); out << numberToString(network->option(Options::ENERGY_PRICE));
        }
        if ( network->option(Options::ENERGY_PRICE_PATTERN) >= 0 )
        {
            if ( !firstGlobal ) out << ",\n";
            firstGlobal = false;
            writeKey(out, n + 4, "pattern"); out << quoted(network->pattern(network->option(Options::ENERGY_PRICE_PATTERN))->name);
        }
        if ( network->option(Options::PUMP_EFFICIENCY) != 0.0 )
        {
            if ( !firstGlobal ) out << ",\n";
            firstGlobal = false;
            writeKey(out, n + 4, "efficiency"); out << numberToString(network->option(Options::PUMP_EFFICIENCY));
        }
        out << "\n";
        indent(out, n + 2);
        out << '}';
        wroteAny = true;
    }

    if ( network->option(Options::PEAKING_CHARGE) != 0.0 )
    {
        if ( wroteAny ) out << ",\n";
        writeKey(out, n + 2, "demandCharge"); out << numberToString(network->option(Options::PEAKING_CHARGE));
        wroteAny = true;
    }

    vector<string> pumpItems;
    for (Link* link : network->links)
    {
        if ( link->type() != Link::PUMP ) continue;
        Pump* pump = static_cast<Pump*>(link);
        if ( !pump->efficCurve && !pump->costPattern && pump->costPerKwh <= 0.0 ) continue;
        ostringstream item;
        item << "{\"id\":" << quoted(link->name);
        if ( pump->efficCurve ) item << ",\"efficiencyCurve\":" << quoted(pump->efficCurve->name);
        if ( pump->costPerKwh > 0.0 ) item << ",\"price\":" << numberToString(pump->costPerKwh);
        if ( pump->costPattern ) item << ",\"pattern\":" << quoted(pump->costPattern->name);
        item << '}';
        pumpItems.push_back(item.str());
    }
    if ( !pumpItems.empty() )
    {
        if ( wroteAny ) out << ",\n";
        writeKey(out, n + 2, "pumps");
        out << "[\n";
        for (size_t i = 0; i < pumpItems.size(); i++)
        {
            indent(out, n + 4);
            out << pumpItems[i];
            if ( i + 1 < pumpItems.size() ) out << ',';
            out << "\n";
        }
        indent(out, n + 2);
        out << ']';
    }

    out << "\n";
    indent(out, n);
    out << '}';
}

void writeQuality(ostream& out, Network* network, int n, bool& first)
{
    vector<string> items;
    for (Node* node : network->nodes)
    {
        if ( node->initQual <= 0.0 ) continue;
        ostringstream item;
        item << "{\"node\":" << quoted(node->name)
             << ",\"quality\":"
             << numberToString(node->initQual * network->ucf(Units::CONCEN))
             << '}';
        items.push_back(item.str());
    }
    if ( items.empty() ) return;
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "quality");
    out << "[\n";
    for (size_t i = 0; i < items.size(); i++)
    {
        indent(out, n + 2);
        out << items[i];
        if ( i + 1 < items.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeSources(ostream& out, Network* network, int n, bool& first)
{
    vector<string> items;
    for (Node* node : network->nodes)
    {
        if ( !node->qualSource || node->qualSource->base <= 0.0 ) continue;
        ostringstream item;
        item << "{\"node\":" << quoted(node->name)
             << ",\"type\":" << quoted(QualSource::SourceTypeWords[node->qualSource->type])
             << ",\"strength\":" << numberToString(node->qualSource->base);
        if ( node->qualSource->pattern ) item << ",\"pattern\":" << quoted(node->qualSource->pattern->name);
        item << '}';
        items.push_back(item.str());
    }
    if ( items.empty() ) return;
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "sources");
    out << "[\n";
    for (size_t i = 0; i < items.size(); i++)
    {
        indent(out, n + 2);
        out << items[i];
        if ( i + 1 < items.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeMixing(ostream& out, Network* network, int n, bool& first)
{
    vector<string> items;
    for (Node* node : network->nodes)
    {
        if ( node->type() != Node::TANK ) continue;
        Tank* tank = static_cast<Tank*>(node);
        ostringstream item;
        item << "{\"tank\":" << quoted(node->name)
             << ",\"model\":" << quoted(TankMixModel::MixingModelWords[tank->mixingModel.type])
             << ",\"fraction\":" << numberToString(tank->mixingModel.fracMixed) << '}';
        items.push_back(item.str());
    }
    if ( items.empty() ) return;
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "mixing");
    out << "[\n";
    for (size_t i = 0; i < items.size(); i++)
    {
        indent(out, n + 2);
        out << items[i];
        if ( i + 1 < items.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

void writeReactions(ostream& out, Network* network, int n, bool& first)
{
    bool hasReaction = false;
    for (Link* link : network->links)
    {
        if ( link->type() != Link::PIPE ) continue;
        Pipe* pipe = static_cast<Pipe*>(link);
        if ( pipe->bulkCoeff != network->option(Options::BULK_COEFF) ||
             pipe->wallCoeff != network->option(Options::WALL_COEFF) )
        {
            hasReaction = true;
            break;
        }
    }
    for (Node* node : network->nodes)
    {
        if ( node->type() != Node::TANK ) continue;
        Tank* tank = static_cast<Tank*>(node);
        if ( tank->bulkCoeff != network->option(Options::BULK_COEFF) )
        {
            hasReaction = true;
            break;
        }
    }
    if ( network->option(Options::BULK_ORDER) != 0.0 ||
         network->option(Options::WALL_ORDER) != 0.0 ||
         network->option(Options::TANK_ORDER) != 0.0 ||
         network->option(Options::BULK_COEFF) != 0.0 ||
         network->option(Options::WALL_COEFF) != 0.0 ||
         network->option(Options::LIMITING_CONCEN) != 0.0 ||
         network->option(Options::ROUGHNESS_FACTOR) != 0.0 )
    {
        hasReaction = true;
    }
    if ( !hasReaction ) return;

    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "reactions");
    out << "{\n";
    bool wroteAny = false;

    writeKey(out, n + 2, "order");
    out << "{\n";
    writeKey(out, n + 4, "bulk"); out << numberToString(network->option(Options::BULK_ORDER)) << ",\n";
    writeKey(out, n + 4, "wall"); out << numberToString(network->option(Options::WALL_ORDER)) << ",\n";
    writeKey(out, n + 4, "tank"); out << numberToString(network->option(Options::TANK_ORDER)) << "\n";
    indent(out, n + 2); out << '}';
    wroteAny = true;

    out << ",\n";
    writeKey(out, n + 2, "global");
    out << "{\n";
    writeKey(out, n + 4, "bulk"); out << numberToString(network->option(Options::BULK_COEFF)) << ",\n";
    writeKey(out, n + 4, "wall"); out << numberToString(network->option(Options::WALL_COEFF)) << "\n";
    indent(out, n + 2); out << '}';

    out << ",\n";
    writeKey(out, n + 2, "limiting"); out << numberToString(network->option(Options::LIMITING_CONCEN)) << ",\n";
    writeKey(out, n + 2, "roughness"); out << numberToString(network->option(Options::ROUGHNESS_FACTOR));

    vector<string> pipeItems;
    for (Link* link : network->links)
    {
        if ( link->type() != Link::PIPE ) continue;
        Pipe* pipe = static_cast<Pipe*>(link);
        if ( pipe->bulkCoeff == network->option(Options::BULK_COEFF) &&
             pipe->wallCoeff == network->option(Options::WALL_COEFF) ) continue;
        ostringstream item;
        item << "{\"id\":" << quoted(link->name);
        if ( pipe->bulkCoeff != network->option(Options::BULK_COEFF) )
        {
            item << ",\"bulk\":" << numberToString(pipe->bulkCoeff);
        }
        if ( pipe->wallCoeff != network->option(Options::WALL_COEFF) )
        {
            item << ",\"wall\":" << numberToString(pipe->wallCoeff);
        }
        item << '}';
        pipeItems.push_back(item.str());
    }
    if ( !pipeItems.empty() )
    {
        out << ",\n";
        writeKey(out, n + 2, "pipes");
        out << "[\n";
        for (size_t i = 0; i < pipeItems.size(); i++)
        {
            indent(out, n + 4);
            out << pipeItems[i];
            if ( i + 1 < pipeItems.size() ) out << ',';
            out << "\n";
        }
        indent(out, n + 2);
        out << ']';
    }

    vector<string> tankItems;
    for (Node* node : network->nodes)
    {
        if ( node->type() != Node::TANK ) continue;
        Tank* tank = static_cast<Tank*>(node);
        if ( tank->bulkCoeff == network->option(Options::BULK_COEFF) ) continue;
        ostringstream item;
        item << "{\"id\":" << quoted(node->name)
             << ",\"bulk\":" << numberToString(tank->bulkCoeff) << '}';
        tankItems.push_back(item.str());
    }
    if ( !tankItems.empty() )
    {
        out << ",\n";
        writeKey(out, n + 2, "tanks");
        out << "[\n";
        for (size_t i = 0; i < tankItems.size(); i++)
        {
            indent(out, n + 4);
            out << tankItems[i];
            if ( i + 1 < tankItems.size() ) out << ',';
            out << "\n";
        }
        indent(out, n + 2);
        out << ']';
    }

    out << "\n";
    indent(out, n);
    out << '}';
}

void writeOptions(ostream& out, Network* network, int n, bool& first)
{
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "options");
    out << "{\n";
    writeKey(out, n + 2, "units"); out << quoted(flowUnitsName(network->option(Options::FLOW_UNITS))) << ",\n";
    writeKey(out, n + 2, "pressure"); out << quoted(pressureUnitsName(network->option(Options::PRESSURE_UNITS))) << ",\n";
    writeKey(out, n + 2, "headloss"); out << quoted(network->option(Options::HEADLOSS_MODEL)) << ",\n";
    writeKey(out, n + 2, "demand");
    out << "{\n";
    writeKey(out, n + 4, "model"); out << quoted(network->option(Options::DEMAND_MODEL)) << ",\n";
    writeKey(out, n + 4, "multiplier"); out << numberToString(network->option(Options::DEMAND_MULTIPLIER));
    if ( network->option(Options::DEMAND_PATTERN) >= 0 )
    {
        out << ",\n";
        writeKey(out, n + 4, "pattern"); out << quoted(network->pattern(network->option(Options::DEMAND_PATTERN))->name);
    }
    out << "\n";
    indent(out, n + 2); out << "},\n";
    writeKey(out, n + 2, "quality");
    out << "{\n";
    writeKey(out, n + 4, "type"); out << quoted(qualityTypeName(network)) << ",\n";
    writeKey(out, n + 4, "name"); out << quoted(network->option(Options::QUAL_NAME)) << ",\n";
    writeKey(out, n + 4, "units"); out << quoted(network->option(Options::QUAL_UNITS_NAME));
    if ( network->option(Options::QUAL_TYPE) == Options::TRACE )
    {
        out << ",\n";
        writeKey(out, n + 4, "traceNode"); out << quoted(network->option(Options::TRACE_NODE_NAME));
    }
    out << "\n";
    indent(out, n + 2); out << "},\n";
    writeKey(out, n + 2, "hydraulicSolver"); out << quoted(network->option(Options::HYD_SOLVER)) << ",\n";
    writeKey(out, n + 2, "matrixSolver"); out << quoted(network->option(Options::MATRIX_SOLVER)) << ",\n";
    writeKey(out, n + 2, "viscosity"); out << numberToString(network->option(Options::KIN_VISCOSITY)) << ",\n";
    writeKey(out, n + 2, "specificGravity"); out << numberToString(network->option(Options::SPEC_GRAVITY)) << ",\n";
    writeKey(out, n + 2, "specificDiffusivity"); out << numberToString(network->option(Options::MOLEC_DIFFUSIVITY)) << ",\n";
    writeKey(out, n + 2, "emitterExponent"); out << numberToString(network->option(Options::EMITTER_EXPONENT)) << ",\n";
    writeKey(out, n + 2, "qualityTolerance"); out << numberToString(network->option(Options::QUAL_TOLERANCE)) << ",\n";
    writeKey(out, n + 2, "relativeAccuracy"); out << numberToString(network->option(Options::RELATIVE_ACCURACY)) << ",\n";
    writeKey(out, n + 2, "headTolerance"); out << numberToString(network->option(Options::HEAD_TOLERANCE)) << ",\n";
    writeKey(out, n + 2, "flowTolerance"); out << numberToString(network->option(Options::FLOW_TOLERANCE)) << ",\n";
    writeKey(out, n + 2, "flowChangeLimit"); out << numberToString(network->option(Options::FLOW_CHANGE_LIMIT)) << ",\n";
    writeKey(out, n + 2, "timeWeight"); out << numberToString(network->option(Options::TIME_WEIGHT)) << ",\n";
    writeKey(out, n + 2, "trials"); out << numberToString(network->option(Options::MAX_TRIALS)) << ",\n";
    writeKey(out, n + 2, "ifUnbalanced"); out << quoted(network->option(Options::IF_UNBALANCED) == Options::CONTINUE ? "CONTINUE" : "STOP") << ",\n";
    writeKey(out, n + 2, "stepSizing"); out << quoted(network->option(Options::STEP_SIZING)) << ",\n";
    writeKey(out, n + 2, "minimumPressure"); out << numberToString(network->option(Options::MINIMUM_PRESSURE)) << ",\n";
    writeKey(out, n + 2, "servicePressure"); out << numberToString(network->option(Options::SERVICE_PRESSURE)) << ",\n";
    writeKey(out, n + 2, "pressureExponent"); out << numberToString(network->option(Options::PRESSURE_EXPONENT)) << ",\n";
    writeKey(out, n + 2, "leakage");
    out << "{\n";
    writeKey(out, n + 4, "model"); out << quoted(network->option(Options::LEAKAGE_MODEL)) << ",\n";
    writeKey(out, n + 4, "coeff1"); out << numberToString(network->option(Options::LEAKAGE_COEFF1)) << ",\n";
    writeKey(out, n + 4, "coeff2"); out << numberToString(network->option(Options::LEAKAGE_COEFF2)) << "\n";
    indent(out, n + 2); out << '}';
    out << "\n";
    indent(out, n); out << '}';
}

void writeTimes(ostream& out, Network* network, int n, bool& first)
{
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "times");
    out << "{\n";
    writeKey(out, n + 2, "duration"); out << quoted(Utilities::getTime(network->option(Options::TOTAL_DURATION))) << ",\n";
    writeKey(out, n + 2, "hydraulicTimestep"); out << quoted(Utilities::getTime(network->option(Options::HYD_STEP))) << ",\n";
    writeKey(out, n + 2, "qualityTimestep"); out << quoted(Utilities::getTime(network->option(Options::QUAL_STEP))) << ",\n";
    writeKey(out, n + 2, "patternTimestep"); out << quoted(Utilities::getTime(network->option(Options::PATTERN_STEP))) << ",\n";
    writeKey(out, n + 2, "patternStart"); out << quoted(Utilities::getTime(network->option(Options::PATTERN_START))) << ",\n";
    writeKey(out, n + 2, "reportTimestep"); out << quoted(Utilities::getTime(network->option(Options::REPORT_STEP))) << ",\n";
    writeKey(out, n + 2, "reportStart"); out << quoted(Utilities::getTime(network->option(Options::REPORT_START))) << ",\n";
    writeKey(out, n + 2, "ruleTimestep"); out << quoted(Utilities::getTime(network->option(Options::RULE_STEP))) << ",\n";
    writeKey(out, n + 2, "startClocktime"); out << quoted(Utilities::getTime(network->option(Options::START_TIME))) << ",\n";
    writeKey(out, n + 2, "statistic"); out << quoted(statisticTypeName(network->option(Options::REPORT_STATISTIC))) << "\n";
    indent(out, n); out << '}';
}

void writeReport(ostream& out, Network* network, int n, bool& first)
{
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "report");
    out << "{\n";
    writeKey(out, n + 2, "summary"); out << (network->option(Options::REPORT_SUMMARY) ? "true" : "false") << ",\n";
    writeKey(out, n + 2, "energy"); out << (network->option(Options::REPORT_ENERGY) ? "true" : "false") << ",\n";
    writeKey(out, n + 2, "status"); out << (network->option(Options::REPORT_STATUS) ? "true" : "false") << ",\n";
    writeKey(out, n + 2, "trials"); out << (network->option(Options::REPORT_TRIALS) ? "true" : "false") << ",\n";
    writeKey(out, n + 2, "nodes");
    if ( network->option(Options::REPORT_NODES) == Options::ALL ) out << quoted("ALL") << ",\n";
    else if ( network->option(Options::REPORT_NODES) == Options::NONE ) out << quoted("NONE") << ",\n";
    else out << quoted("SOME") << ",\n";
    writeKey(out, n + 2, "links");
    if ( network->option(Options::REPORT_LINKS) == Options::ALL ) out << quoted("ALL") << "\n";
    else if ( network->option(Options::REPORT_LINKS) == Options::NONE ) out << quoted("NONE") << "\n";
    else out << quoted("SOME") << "\n";
    indent(out, n); out << '}';
}

void writeCoordinates(ostream& out, Network* network, int n, bool& first)
{
    vector<string> items;
    for (Node* node : network->nodes)
    {
        if ( node->xCoord == 0.0 && node->yCoord == 0.0 ) continue;
        ostringstream item;
        item << "{\"node\":" << quoted(node->name)
             << ",\"x\":" << numberToString(node->xCoord)
             << ",\"y\":" << numberToString(node->yCoord) << '}';
        items.push_back(item.str());
    }
    if ( items.empty() ) return;
    if ( !first ) out << ",\n";
    first = false;
    writeKey(out, n, "coordinates");
    out << "[\n";
    for (size_t i = 0; i < items.size(); i++)
    {
        indent(out, n + 2);
        out << items[i];
        if ( i + 1 < items.size() ) out << ',';
        out << "\n";
    }
    indent(out, n);
    out << ']';
}

} // namespace

JsonProjectWriter::JsonProjectWriter()
{
}

JsonProjectWriter::~JsonProjectWriter()
{
}

int JsonProjectWriter::writeFile(const char* fname, Network* network)
{
    if ( network == 0 ) return 0;

    ofstream fout(fname, ios::out | ios::trunc);
    if ( !fout.is_open() ) return FileError::CANNOT_OPEN_INPUT_FILE;

    fout << "{\n  \"sections\": {\n";
    bool first = true;

    writeTitleSection(fout, network, 4, first);
    writeJunctions(fout, network, 4, first);
    writeReservoirs(fout, network, 4, first);
    writeTanks(fout, network, 4, first);
    writePipes(fout, network, 4, first);
    writePumps(fout, network, 4, first);
    writeValves(fout, network, 4, first);
    writePatterns(fout, network, 4, first);
    writeCurves(fout, network, 4, first);
    writeControls(fout, network, 4, first);
    writeEmitters(fout, network, 4, first);
    writeDemands(fout, network, 4, first);
    writeStatus(fout, network, 4, first);
    writeLeakage(fout, network, 4, first);
    writeEnergy(fout, network, 4, first);
    writeQuality(fout, network, 4, first);
    writeSources(fout, network, 4, first);
    writeMixing(fout, network, 4, first);
    writeReactions(fout, network, 4, first);
    writeOptions(fout, network, 4, first);
    writeTimes(fout, network, 4, first);
    writeReport(fout, network, 4, first);
    writeCoordinates(fout, network, 4, first);

    fout << "\n  }\n}\n";
    fout.close();
    return 0;
}
