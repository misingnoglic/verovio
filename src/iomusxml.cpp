/////////////////////////////////////////////////////////////////////////////
// Name:        iomusxml.cpp
// Author:      Laurent Pugin and Klaus Rettinghaus
// Created:     22/09/2015
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "iomusxml.h"

//----------------------------------------------------------------------------

#include <assert.h>
#include <sstream>

//----------------------------------------------------------------------------

#include "attcomparison.h"
#include "beam.h"
#include "chord.h"
#include "clef.h"
#include "dir.h"
#include "doc.h"
#include "dynam.h"
#include "fermata.h"
#include "hairpin.h"
#include "harm.h"
#include "layer.h"
#include "measure.h"
#include "mordent.h"
#include "mrest.h"
#include "note.h"
#include "octave.h"
#include "pedal.h"
#include "rest.h"
#include "rpt.h"
#include "score.h"
#include "section.h"
#include "slur.h"
#include "space.h"
#include "staff.h"
#include "syl.h"
#include "tempo.h"
#include "text.h"
#include "tie.h"
#include "trill.h"
#include "tuplet.h"
#include "turn.h"
#include "verse.h"
#include "vrv.h"

namespace vrv {

//----------------------------------------------------------------------------
// MusicXmlInput
//----------------------------------------------------------------------------

MusicXmlInput::MusicXmlInput(Doc *doc, std::string filename) : FileInputStream(doc)
{
    m_filename = filename;
}

MusicXmlInput::~MusicXmlInput()
{
}

bool MusicXmlInput::ImportFile()
{
    try {
        m_doc->SetType(Raw);
        pugi::xml_document xmlDoc;
        pugi::xml_parse_result result = xmlDoc.load_file(m_filename.c_str());
        if (!result) {
            return false;
        }
        pugi::xml_node root = xmlDoc.first_child();
        return ReadMusicXml(root);
    }
    catch (char *str) {
        LogError("%s", str);
        return false;
    }
}

bool MusicXmlInput::ImportString(std::string const &musicxml)
{
    try {
        m_doc->SetType(Raw);
        pugi::xml_document xmlDoc;
        xmlDoc.load(musicxml.c_str());
        pugi::xml_node root = xmlDoc.first_child();
        return ReadMusicXml(root);
    }
    catch (char *str) {
        LogError("%s", str);
        return false;
    }
}

//////////////////////////////////////////////////////////////////////////////
// XML helpers

bool MusicXmlInput::HasAttributeWithValue(pugi::xml_node node, std::string attribute, std::string value)
{
    assert(node);

    if (GetAttributeValue(node, attribute) == value) {
        return true;
    }
    return false;
}

bool MusicXmlInput::IsElement(pugi::xml_node node, std::string name)
{
    assert(node);

    if (std::string(node.name()) == name) {
        return true;
    }
    return false;
}

bool MusicXmlInput::HasContentWithValue(pugi::xml_node node, std::string value)
{
    assert(node);

    if (GetContent(node) == value) {
        return true;
    }
    return false;
}

bool MusicXmlInput::HasContent(pugi::xml_node node)
{
    assert(node);

    return (node.text());
}

std::string MusicXmlInput::GetAttributeValue(pugi::xml_node node, std::string attribute)
{
    assert(node);

    if (node.attribute(attribute.c_str())) {
        return std::string(node.attribute(attribute.c_str()).value());
    }
    return "";
}

std::string MusicXmlInput::GetContent(pugi::xml_node node)
{
    assert(node);

    if (node.text()) {
        return std::string(node.text().as_string());
    }
    return "";
}

std::string MusicXmlInput::GetContentOfChild(pugi::xml_node node, std::string child)
{
    pugi::xpath_node childNode = node.select_single_node(child.c_str());
    if (childNode && HasContent(childNode.node())) {
        return GetContent(childNode.node());
    }
    return "";
}

void MusicXmlInput::AddMeasure(Section *section, Measure *measure, int i)
{
    assert(section);
    assert(i >= 0);

    // we just need to add a measure
    if (i == section->GetChildCount(MEASURE)) {
        section->AddChild(measure);
    }
    // otherwise copy the content to the corresponding existing measure
    else if (section->GetChildCount(MEASURE) > i) {
        Measure *existingMeasure = dynamic_cast<Measure *>(section->GetChild(i));
        assert(existingMeasure);
        Object *current;
        for (current = measure->GetFirst(); current; current = measure->GetNext()) {
            Staff *staff = dynamic_cast<Staff *>(measure->Relinquish(current->GetIdx()));
            assert(staff);
            existingMeasure->AddChild(staff);
        }
    }
    // there is a gap, this should not happen
    else {
        LogWarning("measures should be added in the right order");
    }
}

void MusicXmlInput::AddLayerElement(Layer *layer, LayerElement *element)
{
    assert(layer);
    assert(element);

    if (m_elementStack.empty()) {
        layer->AddChild(element);
    }
    else
        (m_elementStack.back()->AddChild(element));
}

Layer *MusicXmlInput::SelectLayer(pugi::xml_node node, vrv::Measure *measure)
{
    int staffNum = 1;
    std::string staffNumStr = GetContentOfChild(node, "staff");
    if (!staffNumStr.empty()) {
        staffNum = atoi(staffNumStr.c_str());
    }
    if ((staffNum < 1) || (staffNum > measure->GetChildCount())) {
        LogWarning("Staff %d cannot be found", staffNum);
        staffNum = 1;
    }
    staffNum--;
    Staff *staff = dynamic_cast<Staff *>(measure->GetChild(staffNum));
    assert(staff);
    // Now look for the layer with the corresponding voice
    int layerNum = 1;
    std::string layerNumStr = GetContentOfChild(node, "voice");
    if (!layerNumStr.empty()) {
        layerNum = atoi(layerNumStr.c_str());
    }
    if (layerNum < 1) {
        LogWarning("Staff %d cannot be found", staffNum);
        layerNum = 1;
    }
    return SelectLayer(layerNum, staff);
}

Layer *MusicXmlInput::SelectLayer(int staffNum, vrv::Measure *measure)
{
    staffNum--;
    Staff *staff = dynamic_cast<Staff *>(measure->GetChild(staffNum));
    assert(staff);
    // layer -1 means the first one
    return SelectLayer(-1, staff);
}

Layer *MusicXmlInput::SelectLayer(int layerNum, Staff *staff)
{
    Layer *layer = NULL;
    // no layer specified, return the first one (if any)
    if (layerNum == -1) {
        if (staff->GetChildCount() > 0) {
            layer = dynamic_cast<Layer *>(staff->GetChild(0));
        }
        // otherwise set @n to 1
        layerNum = 1;
    }
    else {
        AttCommonNComparison comparisonLayer(LAYER, layerNum);
        layer = dynamic_cast<Layer *>(staff->FindChildByAttComparison(&comparisonLayer, 1));
    }
    if (layer) return layer;
    // else add it
    // add at least one layer
    layer = new Layer();
    layer->SetN(layerNum);
    staff->AddChild(layer);
    return layer;
}

void MusicXmlInput::RemoveLastFromStack(ClassId classId)
{
    std::vector<LayerElement *>::reverse_iterator riter;
    for (riter = m_elementStack.rbegin(); riter != m_elementStack.rend(); riter++) {
        if ((*riter)->Is(classId)) {
            m_elementStack.erase((riter + 1).base());
            return;
        }
    }
}

void MusicXmlInput::FillSpace(vrv::Layer *layer, int dur)
{
    std::string durStr;
    while (dur != 0) {
        float quaters = (double)dur / (double)m_ppq;
        if (quaters > 1) quaters = (int)quaters;
        // limit space for now
        if (quaters > 2) quaters = 2;
        durStr = std::to_string(int(4 / quaters));

        Space *space = new Space();
        space->SetDur(space->AttDurationMusical::StrToDuration(durStr));
        AddLayerElement(layer, space);
        dur -= m_ppq * quaters;
    }
}

void MusicXmlInput::GenerateUuid(pugi::xml_node node)
{
    int nr = std::rand();
    char str[17];
    // I do not want to use a stream for doing this!
    snprintf(str, 17, "%016d", nr);

    std::string uuid = StringFormat("%s-%s", node.name(), str).c_str();
    std::transform(uuid.begin(), uuid.end(), uuid.begin(), ::tolower);
    node.append_attribute("xml:id").set_value(uuid.c_str());
}

//////////////////////////////////////////////////////////////////////////////
// Tie and slurs stack management

void MusicXmlInput::OpenTie(Staff *staff, Layer *layer, Note *note, Tie *tie)
{
    tie->SetStartid("#" + note->GetUuid());
    musicxml::OpenTie openTie(staff->GetN(), layer->GetN(), note->GetPname(), note->GetOct());
    m_tieStack.push_back(std::make_pair(tie, openTie));
}

void MusicXmlInput::CloseTie(Staff *staff, Layer *layer, Note *note, bool isClosingTie)
{
    std::vector<std::pair<Tie *, musicxml::OpenTie> >::iterator iter;
    for (iter = m_tieStack.begin(); iter != m_tieStack.end(); iter++) {
        if ((iter->second.m_staffN == staff->GetN()) && (iter->second.m_layerN == layer->GetN())
            && (iter->second.m_pname == note->GetPname()) && iter->second.m_oct == note->GetOct()) {
            iter->first->SetEndid("#" + note->GetUuid());
            m_tieStack.erase(iter);
            if (!isClosingTie) {
                LogWarning("Closing tie for note '%s' even though tie "
                           "/tie[@type='stop'] is missing in the MusicXML",
                    note->GetUuid().c_str());
            }
            return;
        }
    }
}

void MusicXmlInput::OpenSlur(Staff *staff, Layer *layer, int number, Slur *slur)
{
    // No staff is set as slurs can appear across staves
    slur->SetStartid(m_ID);
    musicxml::OpenSlur openSlur(staff->GetN(), layer->GetN(), number);
    m_slurStack.push_back(std::make_pair(slur, openSlur));
}

void MusicXmlInput::CloseSlur(Staff *staff, Layer *layer, int number, LayerElement *element)
{
    std::vector<std::pair<Slur *, musicxml::OpenSlur> >::iterator iter;
    for (iter = m_slurStack.begin(); iter != m_slurStack.end(); iter++) {
        if ((iter->second.m_staffN == staff->GetN()) && (iter->second.m_layerN == layer->GetN())
            && (iter->second.m_number == number)) {
            iter->first->SetEndid("#" + element->GetUuid());
            m_slurStack.erase(iter);
            return;
        }
    }
    LogWarning("Closing slur for element '%s' could not be matched", element->GetUuid().c_str());
}

//////////////////////////////////////////////////////////////////////////////
// Text rendering

void MusicXmlInput::TextRendition(pugi::xpath_node_set words, ControlElement *element)
{
    for (pugi::xpath_node_set::const_iterator it = words.begin(); it != words.end(); ++it) {
        pugi::xml_node textNode = it->node();
        std::string textStr = GetContent(textNode);
        std::string textAlign = GetAttributeValue(textNode, "halign");
        std::string textColor = GetAttributeValue(textNode, "color");
        std::string textFont = GetAttributeValue(textNode, "font-family");
        std::string textStyle = GetAttributeValue(textNode, "font-style");
        std::string textWeight = GetAttributeValue(textNode, "font-weight");
        std::string lang = GetAttributeValue(textNode, "xml:lang");
        Text *text = new Text();
        text->SetText(UTF8to16(textStr));
        if (!textColor.empty() || !textFont.empty() || !textStyle.empty() || !textWeight.empty()) {
            Rend *rend = new Rend();
            if (words.size() > 1 && !lang.empty()) {
                rend->SetLang(lang.c_str());
            }
            if (!textAlign.empty())
                rend->SetHalign(rend->AttHorizontalalign::StrToHorizontalalignment(textAlign.c_str()));
            if (!textColor.empty()) rend->SetColor(textColor.c_str());
            if (!textFont.empty()) rend->SetFontfam(textFont.c_str());
            if (!textStyle.empty()) rend->SetFontstyle(rend->AttTypography::StrToFontstyle(textStyle.c_str()));
            if (!textWeight.empty()) rend->SetFontweight(rend->AttTypography::StrToFontweight(textWeight.c_str()));
            rend->AddChild(text);
            element->AddChild(rend);
        }
        else
            element->AddChild(text);
    }
}

void MusicXmlInput::PrintMetronome(pugi::xml_node metronome, Tempo *tempo)
{
    std::string tempoText = "M.M.";
    if (metronome.select_single_node("per-minute").node()) {
        std::string mm = GetContent(metronome.select_single_node("per-minute").node());
        if (atoi(mm.c_str())) tempo->SetMm(mm.c_str());
        tempoText = tempoText + StringFormat(" = %s", mm.c_str());
    }
    if (metronome.select_single_node("beat-unit").node()) {
        tempo->SetMmUnit(ConvertTypeToDur(GetContent(metronome.select_single_node("beat-unit").node())));
    }
    if (metronome.select_single_node("beat-unit-dot")) {
        tempo->SetMmDots((int)metronome.select_nodes("beat-unit-dot").size());
    }
    if (GetAttributeValue(metronome, "parentheses") == "yes") tempoText = "(" + tempoText + ")";
    Text *text = new Text();
    text->SetText(UTF8to16(tempoText));
    tempo->AddChild(text);
}

//////////////////////////////////////////////////////////////////////////////
// Parsing methods

bool MusicXmlInput::ReadMusicXml(pugi::xml_node root)
{
    assert(root);

    ReadMusicXmlTitle(root);

    Score *score = m_doc->CreateScoreBuffer();
    // the section
    Section *section = new Section();
    score->AddChild(section);

    std::vector<StaffGrp *> m_staffGrpStack;
    StaffGrp *staffGrp = new StaffGrp();
    m_doc->m_scoreDef.AddChild(staffGrp);
    m_staffGrpStack.push_back(staffGrp);

    int staffOffset = 0;
    m_octDis.push_back(0);

    pugi::xpath_node scoreMidiBpm = root.select_single_node("/score-partwise/part[1]/measure[1]/sound[@tempo][1]");
    if (scoreMidiBpm) m_doc->m_scoreDef.SetMidiBpm(atoi(GetAttributeValue(scoreMidiBpm.node(), "tempo").c_str()));

    pugi::xpath_node_set partListChildren = root.select_nodes("/score-partwise/part-list/*");
    for (pugi::xpath_node_set::const_iterator it = partListChildren.begin(); it != partListChildren.end(); ++it) {
        pugi::xpath_node xpathNode = *it;
        if (IsElement(xpathNode.node(), "part-group")) {
            if (HasAttributeWithValue(xpathNode.node(), "type", "start")) {
                StaffGrp *staffGrp = new StaffGrp();
                // read the group-symbol (MEI @symbol)
                std::string groupGymbol = GetContentOfChild(xpathNode.node(), "group-symbol");
                if (groupGymbol == "bracket") {
                    staffGrp->SetSymbol(staffgroupingsym_SYMBOL_bracket);
                }
                else if (groupGymbol == "brace") {
                    staffGrp->SetSymbol(staffgroupingsym_SYMBOL_brace);
                }
                else if (groupGymbol == "line") {
                    staffGrp->SetSymbol(staffgroupingsym_SYMBOL_line);
                }
                // now stack it
                m_staffGrpStack.back()->AddChild(staffGrp);
                m_staffGrpStack.push_back(staffGrp);
            }
            // this is the end of a part-group - we assume each opened part-group to be closed
            else {
                m_staffGrpStack.pop_back();
            }
        }
        else if (IsElement(xpathNode.node(), "score-part")) {
            // get the attributes element of the first measure of the part
            std::string partId = xpathNode.node().attribute("id").as_string();
            std::string xpath = StringFormat("/score-partwise/part[@id='%s']/measure[1]", partId.c_str());
            pugi::xpath_node partFirstMeasure = root.select_single_node(xpath.c_str());
            if (!partFirstMeasure.node().select_single_node("attributes")) {
                LogWarning("Could not find the 'attributes' element in the first "
                           "measure of part '%s'",
                    partId.c_str());
                continue;
            }
            std::string partName = GetContentOfChild(xpathNode.node(), "part-name");
            std::string partAbbr = GetContentOfChild(xpathNode.node(), "part-abbreviation");
            // create the staffDef(s)
            StaffGrp *partStaffGrp = new StaffGrp();
            int nbStaves = ReadMusicXmlPartAttributesAsStaffDef(partFirstMeasure.node(), partStaffGrp, staffOffset);
            // if we have more than one staff in the part we create a new staffGrp
            if (nbStaves > 1) {
                partStaffGrp->SetLabel(partName);
                partStaffGrp->SetLabelAbbr(partAbbr);
                partStaffGrp->SetSymbol(staffgroupingsym_SYMBOL_brace);
                partStaffGrp->SetBarthru(BOOLEAN_true);
                m_staffGrpStack.back()->AddChild(partStaffGrp);
            }
            else {
                StaffDef *staffDef = dynamic_cast<StaffDef *>(partStaffGrp->FindChildByType(STAFFDEF));
                if (staffDef) {
                    staffDef->SetLabel(partName);
                    staffDef->SetLabelAbbr(partAbbr);
                }
                m_staffGrpStack.back()->MoveChildrenFrom(partStaffGrp);
                delete partStaffGrp;
            }

            // find the part and read it
            xpath = StringFormat("/score-partwise/part[@id='%s']", partId.c_str());
            pugi::xpath_node part = root.select_single_node(xpath.c_str());
            if (!part) {
                LogWarning("Could not find the part '%s'", partId.c_str());
                continue;
            }
            ReadMusicXmlPart(part.node(), section, nbStaves, staffOffset);
            // increment the staffOffset for reading the next part
            staffOffset += nbStaves;
        }
        else {
        }
    }
    // here we could check that we have that there is only one staffGrp left in m_staffGrpStack

    Measure *measure = NULL;
    std::vector<std::pair<int, ControlElement *> >::iterator iter;
    for (iter = m_controlElements.begin(); iter != m_controlElements.end(); iter++) {
        if (!measure || (measure->GetN() != iter->first)) {
            AttCommonNComparison comparisonMeasure(MEASURE, iter->first);
            measure = dynamic_cast<Measure *>(section->FindChildByAttComparison(&comparisonMeasure, 1));
        }
        if (!measure) {
            LogWarning(
                "Element '%s' could not be added to measure '%d'", iter->second->GetClassName().c_str(), iter->first);
            continue;
        }
        measure->AddChild(iter->second);
    }

    // assert(m_tieStack.empty());
    m_doc->ConvertToPageBasedDoc();

    return true;
}

void MusicXmlInput::ReadMusicXmlTitle(pugi::xml_node root)
{
    assert(root);
    pugi::xpath_node workTitle = root.select_single_node("/score-partwise/work/work-title");
    pugi::xpath_node movementTitle = root.select_single_node("/score-partwise/movement-title");
    pugi::xml_node meiHead = m_doc->m_header.append_child("meiHead");

    // <fileDesc> /////////////
    pugi::xml_node fileDesc = meiHead.append_child("fileDesc");
    pugi::xml_node titleStmt = fileDesc.append_child("titleStmt");
    pugi::xml_node meiTitle = titleStmt.append_child("title");
    if (movementTitle)
        meiTitle.text().set(GetContent(movementTitle.node()).c_str());
    else if (workTitle)
        meiTitle.text().set(GetContent(workTitle.node()).c_str());

    pugi::xml_node pubStmt = fileDesc.append_child("pubStmt");
    pubStmt.append_child(pugi::node_pcdata);

    pugi::xml_node encodingDesc = meiHead.append_child("encodingDesc");
    GenerateUuid(encodingDesc);
    pugi::xml_node appInfo = encodingDesc.append_child("appInfo");
    GenerateUuid(appInfo);
    pugi::xml_node app = appInfo.append_child("application");
    GenerateUuid(app);
    pugi::xml_node appName = app.append_child("name");
    GenerateUuid(appName);
    appName.append_child(pugi::node_pcdata).set_value("Verovio");
    pugi::xml_node appText = app.append_child("p");
    GenerateUuid(appText);
    appText.append_child(pugi::node_pcdata).set_value("Transcoded from MusicXML");

    // isodate and version
    time_t t = time(0); // get time now
    struct tm *now = localtime(&t);
    std::string dateStr = StringFormat("%d-%02d-%02dT%02d:%02d:%02d", now->tm_year + 1900, now->tm_mon + 1,
        now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);
    app.append_attribute("isodate").set_value(dateStr.c_str());
    app.append_attribute("version").set_value(GetVersion().c_str());
}

int MusicXmlInput::ReadMusicXmlPartAttributesAsStaffDef(pugi::xml_node node, StaffGrp *staffGrp, int staffOffset)
{
    assert(node);
    assert(staffGrp);

    int nbStaves = 1;

    for (pugi::xml_node::iterator it = node.begin(); it != node.end(); ++it) {
        // We read all attribute elements until we reach something else
        // barline, print, and sound elements may be present
        if (!IsElement(*it, "attributes") && !IsElement(*it, "barline") && !IsElement(*it, "print")
            && !IsElement(*it, "sound"))
            break;

        // we do not want to read it again, just change the name
        if (IsElement(*it, "attributes")) it->set_name("mei-read");

        // First get the number of staves in the part
        pugi::xpath_node staves = it->select_single_node("staves");
        if (staves) {
            if (staves.node().text()) {
                int values = atoi(staves.node().text().as_string());
                nbStaves = (values > 0) ? values : 1;
            }
        }

        int i;
        std::string xpath;
        // Create as many staffDef
        for (i = 0; i < nbStaves; i++) {
            // Find or create the staffDef
            StaffDef *staffDef = NULL;
            AttCommonNComparison comparisonStaffDef(STAFFDEF, i + 1 + staffOffset);
            staffDef = dynamic_cast<StaffDef *>(staffGrp->FindChildByAttComparison(&comparisonStaffDef, 1));
            if (!staffDef) {
                staffDef = new StaffDef();
                staffDef->SetN(i + 1 + staffOffset);
                staffGrp->AddChild(staffDef);
                // set initial octave shift
                m_octDis.push_back(0);
            }

            // clef sign - first look if we have a clef-sign with the corresponding staff @number
            pugi::xpath_node clefSign;
            xpath = StringFormat("clef[@number='%d']/sign", i + 1);
            clefSign = it->select_single_node(xpath.c_str());
            // if not, look at a common one
            if (!clefSign) {
                clefSign = it->select_single_node("clef/sign");
            }
            if (clefSign && HasContent(clefSign.node())) {
                staffDef->SetClefShape(
                    staffDef->AttCleffingLog::StrToClefshape(GetContent(clefSign.node()).substr(0, 4)));
            }
            // clef line
            pugi::xpath_node clefLine;
            xpath = StringFormat("clef[@number='%d']/line", i + 1);
            clefLine = it->select_single_node(xpath.c_str());
            if (!clefLine) {
                clefLine = it->select_single_node("clef/line");
            }
            if (clefLine && HasContent(clefLine.node())) {
                staffDef->SetClefLine(staffDef->AttCleffingLog::StrToInt(clefLine.node().text().as_string()));
            }
            // clef octave change
            pugi::xpath_node clefOctaveChange;
            xpath = StringFormat("clef[@number='%d']/clef-octave-change", i + 1);
            clefOctaveChange = it->select_single_node(xpath.c_str());
            if (!clefOctaveChange) {
                clefOctaveChange = it->select_single_node("clef/clef-octave-change");
            }
            if (clefOctaveChange && HasContent(clefOctaveChange.node())) {
                int change = clefOctaveChange.node().text().as_int();
                if (abs(change) == 1)
                    staffDef->SetClefDis(OCTAVE_DIS_8);
                else if (abs(change) == 2)
                    staffDef->SetClefDis(OCTAVE_DIS_15);
                if (change < 0)
                    staffDef->SetClefDisPlace(PLACE_below);
                else if (change > 0)
                    staffDef->SetClefDisPlace(PLACE_above);
            }
            // key sig
            pugi::xpath_node key;
            xpath = StringFormat("key[@number='%d']", i + 1);
            key = it->select_single_node(xpath.c_str());
            if (!key) {
                key = it->select_single_node("key");
            }
            if (key) {
                if (key.node().select_single_node("fifths")) {
                    int fifths = atoi(key.node().select_single_node("fifths").node().text().as_string());
                    std::string keySig;
                    if (fifths < 0)
                        keySig = StringFormat("%df", abs(fifths));
                    else if (fifths > 0)
                        keySig = StringFormat("%ds", fifths);
                    else
                        keySig = "0";
                    staffDef->SetKeySig(staffDef->AttKeySigDefaultLog::StrToKeysignature(keySig));
                }
                else if (key.node().select_single_node("key-step")) {
                    staffDef->SetKeySig(KEYSIGNATURE_mixed);
                }
                if (key.node().select_single_node("mode")) {
                    staffDef->SetKeyMode(staffDef->AttKeySigDefaultLog::StrToMode(
                        key.node().select_single_node("mode").node().text().as_string()));
                }
            }
            // staff details
            pugi::xpath_node staffDetails;
            xpath = StringFormat("staff-details[@number='%d']", i + 1);
            staffDetails = it->select_single_node(xpath.c_str());
            if (!staffDetails) {
                staffDetails = it->select_single_node("staff-details");
            }
            std::string linesStr = staffDetails.node().select_single_node("staff-lines").node().text().as_string();
            if (!linesStr.empty()) {
                staffDef->SetLines(atoi(linesStr.c_str()));
            }
            else
                staffDef->SetLines(5);
            std::string scaleStr = staffDetails.node().select_single_node("staff-size").node().text().as_string();
            if (!scaleStr.empty()) {
                staffDef->SetScale(staffDef->AttScalable::StrToPercent(scaleStr));
            }
            pugi::xpath_node staffTuning = staffDetails.node().select_single_node("staff-tuning");
            if (staffTuning) {
                staffDef->SetNotationtype(NOTATIONTYPE_tab);
            }
            // time
            pugi::xpath_node time;
            xpath = StringFormat("time[@number='%d']", i + 1);
            time = it->select_single_node(xpath.c_str());
            if (!time) {
                time = it->select_single_node("time");
            }
            if (time) {
                std::string symbol = GetAttributeValue(time.node(), "symbol");
                if (!symbol.empty()) {
                    if (symbol == "cut" || symbol == "common")
                        staffDef->SetMeterSym(staffDef->AttMeterSigDefaultVis::StrToMetersign(symbol.c_str()));
                    else if (symbol == "single-number")
                        staffDef->SetMeterRend(meterSigDefaultVis_METERREND_num);
                    else
                        staffDef->SetMeterRend(meterSigDefaultVis_METERREND_norm);
                }
                if (time.node().select_nodes("beats").size() > 1) {
                    LogWarning("Compound meter signatures are not supported");
                }
                pugi::xpath_node beats = time.node().select_single_node("beats");
                if (beats && HasContent(beats.node())) {
                    m_meterCount = beats.node().text().as_int();
                    // staffDef->AttMeterSigDefaultLog::StrToInt(beats.node().text().as_string());
                    // this is a little "hack", until libMEI is fixed
                    std::string compound = beats.node().text().as_string();
                    if (compound.find("+") != std::string::npos) {
                        m_meterCount += atoi(compound.substr(compound.find("+")).c_str());
                        LogWarning("Compound time is not supported");
                    }
                    staffDef->SetMeterCount(m_meterCount);
                }
                pugi::xpath_node beatType = time.node().select_single_node("beat-type");
                if (beatType && HasContent(beatType.node())) {
                    staffDef->SetMeterUnit(
                        staffDef->AttMeterSigDefaultLog::StrToInt(beatType.node().text().as_string()));
                }
            }
            // transpose
            pugi::xpath_node transpose;
            xpath = StringFormat("transpose[@number='%d']", i + 1);
            transpose = it->select_single_node(xpath.c_str());
            if (!transpose) {
                transpose = it->select_single_node("transpose");
            }
            if (transpose) {
                staffDef->SetTransDiat(atoi(GetContentOfChild(transpose.node(), "diatonic").c_str()));
                staffDef->SetTransSemi(atoi(GetContentOfChild(transpose.node(), "chromatic").c_str()));
            }
            // ppq
            pugi::xpath_node divisions = it->select_single_node("divisions");
            if (divisions) m_ppq = atoi(GetContent(divisions.node()).c_str());
        }
    }

    return nbStaves;
}

bool MusicXmlInput::ReadMusicXmlPart(pugi::xml_node node, Section *section, int nbStaves, int staffOffset)
{
    assert(node);
    assert(section);

    pugi::xpath_node_set measures = node.select_nodes("measure");
    if (measures.size() == 0) {
        LogWarning("No measure to load");
        return false;
    }

    int i = 0;
    for (pugi::xpath_node_set::const_iterator it = measures.begin(); it != measures.end(); ++it) {
        pugi::xpath_node xmlMeasure = *it;
        Measure *measure = new Measure();
        ReadMusicXmlMeasure(xmlMeasure.node(), measure, nbStaves, staffOffset);
        // Add the measure to the system - if already there from a previous part we'll just merge the content
        AddMeasure(section, measure, i);
        i++;
    }
    return false;
}

bool MusicXmlInput::ReadMusicXmlMeasure(pugi::xml_node node, Measure *measure, int nbStaves, int staffOffset)
{
    assert(node);
    assert(measure);

    int measureNum = atoi(GetAttributeValue(node, "number").c_str());
    if (measure != NULL) measure->SetN(measureNum);

    int i = 0;
    for (i = 0; i < nbStaves; i++) {
        // the staff @n must take into account the staffOffset
        Staff *staff = new Staff();
        staff->SetN(i + 1 + staffOffset);
        measure->AddChild(staff);
        // layers will be added in SelectLayer
    }

    // Normally the stack should be empty
    // LogDebug("Measure %s", GetAttributeValue(node, "number").c_str());
    // assert(m_elementStack.empty());
    m_elementStack.clear();

    // reset measure time
    m_durTotal = 0;

    // read the content of the measure
    for (pugi::xml_node::iterator it = node.begin(); it != node.end(); ++it) {
        if (IsElement(*it, "attributes")) {
            ReadMusicXmlAttributes(*it, measure, measureNum);
        }
        else if (IsElement(*it, "backup")) {
            ReadMusicXmlBackup(*it, measure, measureNum);
        }
        else if (IsElement(*it, "barline")) {
            ReadMusicXmlBarLine(*it, measure, measureNum);
        }
        else if (IsElement(*it, "direction")) {
            ReadMusicXmlDirection(*it, measure, measureNum);
        }
        else if (IsElement(*it, "forward")) {
            ReadMusicXmlForward(*it, measure, measureNum);
        }
        else if (IsElement(*it, "harmony")) {
            ReadMusicXmlHarmony(*it, measure, measureNum);
        }
        else if (IsElement(*it, "note")) {
            ReadMusicXmlNote(*it, measure, measureNum);
        }
        else if (IsElement(*it, "print")) {
            ReadMusicXmlPrint(*it, measure, measureNum);
        }
    }

    return true;
}

void MusicXmlInput::ReadMusicXmlAttributes(pugi::xml_node node, Measure *measure, int measureNum)
{
    // read clef changes as MEI clef
    pugi::xpath_node clef = node.select_single_node("clef");
    if (clef) {
        // check if we have a staff number
        std::string numberStr = GetAttributeValue(clef.node(), "number");
        int staffNum = (numberStr.empty()) ? 1 : atoi(numberStr.c_str());
        Layer *layer = SelectLayer(staffNum, measure);
        pugi::xpath_node clefSign = clef.node().select_single_node("sign");
        pugi::xpath_node clefLine = clef.node().select_single_node("line");
        if (clefSign && clefLine) {
            Clef *meiClef = new Clef();
            meiClef->SetShape(meiClef->AttClefshape::StrToClefshape(GetContent(clefSign.node())));
            meiClef->SetLine(meiClef->AttClefshape::StrToInt(clefLine.node().text().as_string()));
            // clef octave change
            pugi::xpath_node clefOctaveChange = clef.node().select_single_node("clef-octave-change");
            if (clefOctaveChange && HasContent(clefOctaveChange.node())) {
                int change = clefOctaveChange.node().text().as_int();
                if (abs(change) == 1)
                    meiClef->SetDis(OCTAVE_DIS_8);
                else if (abs(change) == 2)
                    meiClef->SetDis(OCTAVE_DIS_15);
                if (change < 0)
                    meiClef->SetDisPlace(PLACE_below);
                else
                    meiClef->SetDisPlace(PLACE_above);
            }
            AddLayerElement(layer, meiClef);
        }
    }

    pugi::xpath_node measureRepeat = node.select_single_node("measure-style/measure-repeat");
    if (measureRepeat) {
        if (GetAttributeValue(measureRepeat.node(), "type") == "start")
            m_mRpt = true;
        else
            m_mRpt = false;
    }
}

void MusicXmlInput::ReadMusicXmlBackup(pugi::xml_node node, Measure *measure, int measureNum)
{
    assert(node);
    assert(measure);

    m_durTotal -= atoi(GetContentOfChild(node, "duration").c_str());

    pugi::xpath_node nextNote = node.next_sibling("note");
    if (nextNote && m_durTotal > 0) {
        // We need a <space> if a note follows that starts not at the beginning of the measure
        Layer *layer = new Layer();
        if (!node.select_single_node("voice")) layer = SelectLayer(nextNote.node(), measure);
        FillSpace(layer, m_durTotal);
    }
}

void MusicXmlInput::ReadMusicXmlBarLine(pugi::xml_node node, Measure *measure, int measureNum)
{
    assert(node);
    assert(measure);

    Staff *staff = dynamic_cast<Staff *>(measure->GetChild(0));
    assert(staff);

    data_BARRENDITION barRendition = BARRENDITION_NONE;
    std::string barStyle = GetContentOfChild(node, "bar-style");
    pugi::xpath_node repeat = node.select_single_node("repeat");
    if (!barStyle.empty()) {
        barRendition = ConvertStyleToRend(barStyle, repeat);
        if (HasAttributeWithValue(node, "location", "left")) {
            measure->SetLeft(barRendition);
        }
        else if (HasAttributeWithValue(node, "location", "middle")) {
            LogWarning("Unsupported barline location 'middle'");
        }
        else {
            measure->SetRight(barRendition);
        }
    }
    pugi::xpath_node ending = node.select_single_node("ending");
    if (ending) {
        LogWarning("Endings not supported");
    }
    // fermatas
    pugi::xpath_node xmlFermata = node.select_single_node("fermata");
    if (xmlFermata) {
        Fermata *fermata = new Fermata();
        m_controlElements.push_back(std::make_pair(measureNum, fermata));
        if (HasAttributeWithValue(node, "location", "left")) {
            fermata->SetTstamp(0);
        }
        else if (HasAttributeWithValue(node, "location", "middle")) {
            LogWarning("Unsupported barline location 'middle'");
        }
        else {
            fermata->SetTstamp(m_meterCount + 1);
        }
        fermata->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
        // color
        std::string colorStr = GetAttributeValue(xmlFermata.node(), "color");
        if (!colorStr.empty()) fermata->SetColor(colorStr.c_str());
        // shape
        fermata->SetShape(ConvertFermataShape(GetContent(xmlFermata.node())));
        // form and place
        if (HasAttributeWithValue(xmlFermata.node(), "type", "inverted")) {
            fermata->SetForm(fermataVis_FORM_inv);
            fermata->SetPlace(STAFFREL_below);
        }
        else if (HasAttributeWithValue(xmlFermata.node(), "type", "upright")) {
            fermata->SetForm(fermataVis_FORM_norm);
            fermata->SetPlace(STAFFREL_above);
        }
    }
}

void MusicXmlInput::ReadMusicXmlDirection(pugi::xml_node node, Measure *measure, int measureNum)
{
    assert(node);
    assert(measure);

    pugi::xpath_node type = node.select_single_node("direction-type");
    std::string placeStr = GetAttributeValue(node, "placement");
    pugi::xpath_node_set words = type.node().select_nodes("words");

    // Directive
    if (words.size() != 0 && !node.select_single_node("sound[@tempo]")) {
        Dir *dir = new Dir();
        if (words.size() == 1) {
            std::string lang = GetAttributeValue(words.first().node(), "xml:lang");
            if (!lang.empty()) dir->SetLang(lang.c_str());
        }
        if (!placeStr.empty()) dir->SetPlace(dir->AttPlacement::StrToStaffrel(placeStr.c_str()));
        TextRendition(words, dir);
        m_controlElements.push_back(std::make_pair(measureNum, dir));
        m_dirStack.push_back(dir);
    }

    // Dynamics
    pugi::xpath_node dynam = type.node().select_single_node("dynamics");
    if (dynam) {
        std::string dynamStr = GetContentOfChild(dynam.node(), "other-dynamics");
        if (dynamStr.empty()) dynamStr = dynam.node().first_child().name();
        Dynam *dynam = new Dynam();
        if (!placeStr.empty()) dynam->SetPlace(dynam->AttPlacement::StrToStaffrel(placeStr.c_str()));
        Text *text = new Text();
        text->SetText(UTF8to16(dynamStr));
        dynam->AddChild(text);
        m_controlElements.push_back(std::make_pair(measureNum, dynam));
        m_dynamStack.push_back(dynam);
    }

    // Hairpins
    pugi::xpath_node wedge = type.node().select_single_node("wedge");
    if (wedge) {
        int hairpinNumber = atoi(GetAttributeValue(wedge.node(), "number").c_str());
        hairpinNumber = (hairpinNumber < 1) ? 1 : hairpinNumber;
        if (HasAttributeWithValue(wedge.node(), "type", "stop")) {
            std::vector<std::pair<Hairpin *, musicxml::OpenHairpin> >::iterator iter;
            for (iter = m_hairpinStack.begin(); iter != m_hairpinStack.end(); iter++) {
                if (iter->second.m_dirN == hairpinNumber) {
                    iter->first->SetEndid(iter->second.m_endID);
                    m_hairpinStack.erase(iter);
                    return;
                }
            }
        }
        else {
            Hairpin *hairpin = new Hairpin();
            musicxml::OpenHairpin openHairpin(hairpinNumber, "");
            if (HasAttributeWithValue(wedge.node(), "type", "crescendo")) {
                hairpin->SetForm(hairpinLog_FORM_cres);
            }
            else if (HasAttributeWithValue(wedge.node(), "type", "diminuendo")) {
                hairpin->SetForm(hairpinLog_FORM_dim);
            }
            std::string colorStr = GetAttributeValue(wedge.node(), "color");
            if (!colorStr.empty()) hairpin->SetColor(colorStr.c_str());
            if (!placeStr.empty()) hairpin->SetPlace(hairpin->AttPlacement::StrToStaffrel(placeStr.c_str()));
            m_controlElements.push_back(std::make_pair(measureNum, hairpin));
            m_hairpinStack.push_back(std::make_pair(hairpin, openHairpin));
        }
    }

    // Ottava
    pugi::xpath_node xmlShift = type.node().select_single_node("octave-shift");
    if (xmlShift) {
        pugi::xpath_node staffNode = node.select_single_node("staff");
        int staffN = (!staffNode) ? 1 : atoi(GetContent(staffNode.node()).c_str());
        if (HasAttributeWithValue(xmlShift.node(), "type", "stop")) {
            m_octDis[staffN] = 0;
            std::vector<std::pair<int, ControlElement *> >::iterator iter;
            for (iter = m_controlElements.begin(); iter != m_controlElements.end(); iter++) {
                if (iter->second->Is(OCTAVE)) {
                    Octave *octave = dynamic_cast<Octave *>(iter->second);
                    std::vector<int> staffAttr = octave->GetStaff();
                    if (std::find(staffAttr.begin(), staffAttr.end(), staffN) != staffAttr.end()
                        && !octave->HasEndid()) {
                        octave->SetEndid(m_ID);
                    }
                }
            }
        }
        else {
            Octave *octave = new Octave();
            std::string colorStr = GetAttributeValue(xmlShift.node(), "color");
            if (!colorStr.empty()) octave->SetColor(colorStr.c_str());
            if (!placeStr.empty()) octave->SetDisPlace(octave->AttOctavedisplacement::StrToPlace(placeStr.c_str()));
            octave->SetStaff(octave->AttStaffident::StrToXsdPositiveIntegerList(std::to_string(staffN)));
            octave->SetDis(
                octave->AttOctavedisplacement::StrToOctaveDis(GetAttributeValue(xmlShift.node(), "size").c_str()));
            m_octDis[staffN] = (atoi(GetAttributeValue(xmlShift.node(), "size").c_str()) + 2) / 8;
            if (HasAttributeWithValue(xmlShift.node(), "type", "down")) {
                octave->SetDisPlace(PLACE_below);
                m_octDis[staffN] = -1 * m_octDis[staffN];
            }
            else
                octave->SetDisPlace(PLACE_above);
            m_controlElements.push_back(std::make_pair(measureNum, octave));
            m_octaveStack.push_back(octave);
        }
    }

    // Pedal
    pugi::xpath_node xmlPedal = type.node().select_single_node("pedal");
    if (xmlPedal) {
        Pedal *pedal = new Pedal();
        if (!placeStr.empty()) pedal->SetPlace(pedal->AttPlacement::StrToStaffrel(placeStr.c_str()));
        std::string pedalType = GetAttributeValue(xmlPedal.node(), "type");
        if (!pedalType.empty()) pedal->SetDir(ConvertPedalTypeToDir(pedalType));
        if (pedalType == "stop") pedal->SetStartid(m_ID);
        m_controlElements.push_back(std::make_pair(measureNum, pedal));
        m_pedalStack.push_back(pedal);
    }

    // Tempo
    pugi::xpath_node metronome = type.node().select_single_node("metronome");
    if (node.select_single_node("sound[@tempo]") || metronome) {
        Tempo *tempo = new Tempo();
        if (words.size() == 1) {
            std::string lang = GetAttributeValue(words.first().node(), "xml:lang");
            if (!lang.empty()) tempo->SetLang(lang.c_str());
        }
        if (!placeStr.empty()) tempo->SetPlace(tempo->AttPlacement::StrToStaffrel(placeStr.c_str()));
        if (words.size() != 0) TextRendition(words, tempo);
        if (metronome)
            PrintMetronome(metronome.node(), tempo);
        else
            tempo->SetMidiBpm(atoi(GetAttributeValue(node.select_single_node("sound").node(), "tempo").c_str()));
        m_controlElements.push_back(std::make_pair(measureNum, tempo));
        m_tempoStack.push_back(tempo);
    }

    // other cases
    if (words.size() == 0 && !dynam && !metronome && !xmlShift && !xmlPedal && !wedge) {
        LogWarning("Unsupported direction-type '%s'", type.node().first_child().name());
    }
}

void MusicXmlInput::ReadMusicXmlForward(pugi::xml_node node, Measure *measure, int measureNum)
{
    assert(node);
    assert(measure);

    m_durTotal += atoi(GetContentOfChild(node, "duration").c_str());

    Layer *layer = SelectLayer(node, measure);

    pugi::xpath_node prevNote = node.select_single_node("preceding-sibling::note[1]");
    pugi::xpath_node nextNote = node.select_single_node("following-sibling::note[1]");
    if (nextNote) {
        // We need a <space> if a note follows
        if (!node.select_single_node("voice")) layer = SelectLayer(nextNote.node(), measure);
        FillSpace(layer, atoi(GetContentOfChild(node, "duration").c_str()));
    }
    else if (!prevNote && !node.select_single_node("preceding-sibling::backup")) {
        // If there is no previous or following note in the first layer, the measure seems to be empty
        // an invisible mRest is used, which should be replaced by mSpace, when available
        MRest *mRest = new MRest();
        mRest->SetVisible(BOOLEAN_false);
        AddLayerElement(layer, mRest);
    }
}

void MusicXmlInput::ReadMusicXmlHarmony(pugi::xml_node node, Measure *measure, int measureNum)
{
    assert(node);
    assert(measure);

    std::string placeStr = GetAttributeValue(node, "placement");
    std::string typeStr = GetAttributeValue(node, "type");

    std::string harmText = GetContentOfChild(node, "root/root-step");
    pugi::xpath_node alter = node.select_single_node("root/root-alter");
    if (alter) {
        if (GetContent(alter.node()) == "-1")
            harmText = harmText + "♭";
        else if (GetContent(alter.node()) == "0")
            harmText = harmText + "♮";
        else if (GetContent(alter.node()) == "1")
            harmText = harmText + "♯";
    }
    pugi::xpath_node kind = node.select_single_node("kind");
    if (kind) harmText = harmText + GetAttributeValue(kind.node(), "text").c_str();
    Harm *harm = new Harm();
    Text *text = new Text();
    if (!placeStr.empty()) harm->SetPlace(harm->AttPlacement::StrToStaffrel(placeStr.c_str()));
    if (!typeStr.empty()) harm->SetType(typeStr.c_str());
    text->SetText(UTF8to16(harmText));
    harm->AddChild(text);
    m_controlElements.push_back(std::make_pair(measureNum, harm));
    m_harmStack.push_back(harm);
}

void MusicXmlInput::ReadMusicXmlNote(pugi::xml_node node, Measure *measure, int measureNum)
{
    assert(node);
    assert(measure);

    Layer *layer = SelectLayer(node, measure);
    assert(layer);

    Staff *staff = dynamic_cast<Staff *>(layer->GetFirstParent(STAFF));
    assert(staff);

    LayerElement *element = NULL;

    // add duration to measure time
    if (!node.select_single_node("chord")) m_durTotal += atoi(GetContentOfChild(node, "duration").c_str());

    // for measure repeats add a single <mRpt> and return
    if (m_mRpt) {
        MRpt *mRpt = dynamic_cast<MRpt *>((*layer).GetFirst(MRPT));
        if (!mRpt) {
            mRpt = new MRpt();
            AddLayerElement(layer, mRpt);
        }
        return;
    }

    std::string noteColor = GetAttributeValue(node, "color");

    pugi::xpath_node notations = node.select_single_node("notations[not(@print-object='no')]");

    bool cue = false;
    if (node.select_single_node("cue") || node.select_single_node("type[@size='cue']")) cue = true;

    // duration string and dots
    std::string typeStr = GetContentOfChild(node, "type");
    int dots = (int)node.select_nodes("dot").size();

    // tremolos
    pugi::xpath_node tremolo = notations.node().select_single_node("ornaments/tremolo");
    std::string tremSlashNum = "0";
    if (tremolo) {
        if (HasAttributeWithValue(tremolo.node(), "type", "single")) {
            BTrem *bTrem = new BTrem();
            AddLayerElement(layer, bTrem);
            m_elementStack.push_back(bTrem);
            tremSlashNum = GetContent(tremolo.node());
        }
        else if (HasAttributeWithValue(tremolo.node(), "type", "start")) {
            FTrem *fTrem = new FTrem();
            AddLayerElement(layer, fTrem);
            m_elementStack.push_back(fTrem);
            fTrem->SetSlash(atoi(GetContent(tremolo.node()).c_str()));
        }
    }

    // beam start
    bool beamStart = node.select_single_node("beam[@number='1'][text()='begin']");
    if (beamStart) {
        Beam *beam = new Beam();
        AddLayerElement(layer, beam);
        m_elementStack.push_back(beam);
    }

    // tuplet start
    // For now tuplet with beam if starting at the same time. However, this will
    // quite likely not work if we have a tuplet over serveral beams. We would need to check which
    // one is ending first in order to determine which one is on top of the hierarchy.
    // Also, it is not 100% sure that we can represent them as tuplet and beam elements.
    pugi::xpath_node tupletStart = notations.node().select_single_node("tuplet[@type='start']");
    if (tupletStart) {
        Tuplet *tuplet = new Tuplet();
        AddLayerElement(layer, tuplet);
        m_elementStack.push_back(tuplet);
        pugi::xpath_node actualNotes = node.select_single_node("time-modification/actual-notes");
        pugi::xpath_node normalNotes = node.select_single_node("time-modification/normal-notes");
        if (actualNotes && normalNotes) {
            tuplet->SetNum(atoi(GetContent(actualNotes.node()).c_str()));
            tuplet->SetNumbase(atoi(GetContent(normalNotes.node()).c_str()));
        }
        if (!GetAttributeValue(tupletStart.node(), "placement").empty()) {
            tuplet->SetNumPlace(tuplet->AttTupletVis::StrToPlace(GetAttributeValue(tupletStart.node(), "placement")));
            tuplet->SetBracketPlace(
                tuplet->AttTupletVis::StrToPlace(GetAttributeValue(tupletStart.node(), "placement")));
        }
        tuplet->SetNumFormat(ConvertTupletNumberValue(GetAttributeValue(tupletStart.node(), "show-number")));
        if (HasAttributeWithValue(tupletStart.node(), "show-number", "none")) tuplet->SetNumVisible(BOOLEAN_false);
        tuplet->SetBracketVisible(ConvertWordToBool(GetAttributeValue(tupletStart.node(), "bracket")));
    }

    pugi::xpath_node rest = node.select_single_node("rest");
    if (rest) {
        std::string stepStr = GetContentOfChild(rest.node(), "display-step");
        std::string octaveStr = GetContentOfChild(rest.node(), "display-octave");
        if (HasAttributeWithValue(node, "print-object", "no")) {
            Space *space = new Space();
            element = space;
            space->SetDur(ConvertTypeToDur(typeStr));
            AddLayerElement(layer, space);
        }
        // we assume /note without /type to be mRest
        else if (typeStr.empty() || HasAttributeWithValue(rest.node(), "measure", "yes")) {
            MRest *mRest = new MRest();
            element = mRest;
            if (cue) mRest->SetSize(SIZE_cue);
            if (!stepStr.empty()) mRest->SetPloc(ConvertStepToPitchName(stepStr));
            if (!octaveStr.empty()) mRest->SetOloc(atoi(octaveStr.c_str()));
            AddLayerElement(layer, mRest);
        }
        else {
            Rest *rest = new Rest();
            element = rest;
            rest->SetDur(ConvertTypeToDur(typeStr));
            if (dots > 0) rest->SetDots(dots);
            if (cue) rest->SetSize(SIZE_cue);
            if (!stepStr.empty()) rest->SetPloc(ConvertStepToPitchName(stepStr));
            if (!octaveStr.empty()) rest->SetOloc(atoi(octaveStr.c_str()));
            AddLayerElement(layer, rest);
        }
    }
    else {
        Note *note = new Note();
        element = note;
        note->SetVisible(ConvertWordToBool(GetAttributeValue(node, "print-object")));
        if (!noteColor.empty()) note->SetColor(noteColor.c_str());

        // accidental
        pugi::xpath_node accidental = node.select_single_node("accidental");
        if (accidental) {
            Accid *accid = new Accid();
            accid->SetAccid(ConvertAccidentalToAccid(GetContent(accidental.node())));
            std::string accidColor = GetAttributeValue(accidental.node(), "color");
            if (!accidColor.empty()) accid->SetColor(accidColor.c_str());
            if (HasAttributeWithValue(accidental.node(), "cautionary", "yes")) accid->SetFunc(accidLog_FUNC_caution);
            if (HasAttributeWithValue(accidental.node(), "editorial", "yes")) accid->SetFunc(accidLog_FUNC_edit);
            if (HasAttributeWithValue(accidental.node(), "bracket", "yes")) accid->SetEnclose(ENCLOSURE_brack);
            if (HasAttributeWithValue(accidental.node(), "parentheses", "yes")) accid->SetEnclose(ENCLOSURE_paren);
            note->AddChild(accid);
        }

        // stem direction - taken into account below for the chord or the note
        data_STEMDIRECTION stemDir = STEMDIRECTION_NONE;
        std::string stemDirStr = GetContentOfChild(node, "stem");
        if (stemDirStr == "down") {
            stemDir = STEMDIRECTION_down;
        }
        else if (stemDirStr == "up") {
            stemDir = STEMDIRECTION_up;
        }

        // pitch and octave
        pugi::xpath_node pitch = node.select_single_node("pitch");
        if (pitch) {
            std::string stepStr = GetContentOfChild(pitch.node(), "step");
            if (!stepStr.empty()) note->SetPname(ConvertStepToPitchName(stepStr));
            std::string octaveStr = GetContentOfChild(pitch.node(), "octave");
            if (!octaveStr.empty()) {
                if (m_octDis[staff->GetN()] != 0) {
                    note->SetOct(atoi(octaveStr.c_str()) + m_octDis[staff->GetN()]);
                    note->SetOctGes(atoi(octaveStr.c_str()));
                }
                else
                    note->SetOct(atoi(octaveStr.c_str()));
            }
            std::string alterStr = GetContentOfChild(pitch.node(), "alter");
            if (!accidental && !alterStr.empty()) {
                Accid *accid = dynamic_cast<Accid *>(note->GetFirst(ACCID));
                if (!accid) {
                    accid = new Accid();
                    note->AddChild(accid);
                }
                accid->SetAccidGes(ConvertAlterToAccid(std::atof(alterStr.c_str())));
            }
        }

        // notehead
        pugi::xpath_node notehead = node.select_single_node("notehead");
        if (notehead) {
            // if (HasAttributeWithValue(notehead.node(), "parentheses", "yes")) note->SetEnclose(ENCLOSURE_paren);
        }

        // look at the next note to see if we are starting or ending a chord
        pugi::xpath_node nextNote = node.select_single_node("./following-sibling::note");
        bool nextIsChord = false;
        if (nextNote.node().select_single_node("chord")) nextIsChord = true;
        // create the chord if we are starting a new chord
        if (nextIsChord) {
            if (m_elementStack.empty() || !m_elementStack.back()->Is(CHORD)) {
                Chord *chord = new Chord();
                chord->SetDur(ConvertTypeToDur(typeStr));
                if (dots > 0) chord->SetDots(dots);
                chord->SetStemDir(stemDir);
                if (cue) chord->SetSize(SIZE_cue);
                if (tremSlashNum != "0") chord->SetStemMod(chord->AttStems::StrToStemmodifier(tremSlashNum + "slash"));
                AddLayerElement(layer, chord);
                m_elementStack.push_back(chord);
                element = chord;
            }
        }

        // grace notes
        pugi::xpath_node grace = node.select_single_node("grace");
        if (grace) {
            std::string slashStr = GetAttributeValue(grace.node(), "slash");
            if (slashStr == "no") {
                note->SetGrace(GRACE_acc);
            }
            else if (slashStr == "yes") {
                note->SetGrace(GRACE_unacc);
                note->SetStemMod(STEMMODIFIER_1slash);
            }
            else {
                note->SetGrace(GRACE_unknown);
            }
        }

        // set attributes to the note if we are not in a chord
        if (m_elementStack.empty() || !m_elementStack.back()->Is(CHORD)) {
            note->SetDur(ConvertTypeToDur(typeStr));
            if (dots > 0) note->SetDots(dots);
            note->SetStemDir(stemDir);
            if (cue) note->SetSize(SIZE_cue);
            if (tremSlashNum != "0") note->SetStemMod(note->AttStems::StrToStemmodifier(tremSlashNum + "slash"));
        }

        // verse / syl
        pugi::xpath_node_set lyrics = node.select_nodes("lyric");
        for (pugi::xpath_node_set::const_iterator it = lyrics.begin(); it != lyrics.end(); ++it) {
            pugi::xml_node lyric = it->node();
            int lyricNumber = atoi(GetAttributeValue(lyric, "number").c_str());
            lyricNumber = (lyricNumber < 1) ? 1 : lyricNumber;
            std::string lyricName = GetAttributeValue(lyric, "name");
            std::string lyricColor = GetAttributeValue(lyric, "color");
            Verse *verse = new Verse();
            verse->SetN(lyricNumber);
            if (!lyricColor.empty()) verse->SetColor(lyricColor.c_str());
            // if (!lyricName.empty()) verse->SetLabel(lyricName.c_str());
            for (pugi::xml_node textNode = lyric.child("text"); textNode; textNode = textNode.next_sibling("text")) {
                if (GetAttributeValue(lyric, "print-object") != "no") {
                    // std::string textColor = GetAttributeValue(textNode.node(), "color");
                    std::string textStyle = GetAttributeValue(textNode, "font-style");
                    std::string textWeight = GetAttributeValue(textNode, "font-weight");
                    std::string lang = GetAttributeValue(textNode, "xml:lang");
                    std::string textStr = GetContent(textNode);
                    Syl *syl = new Syl();
                    if (!lang.empty()) syl->SetLang(lang.c_str());
                    if (lyric.select_single_node("extend")) {
                        syl->SetCon(sylLog_CON_u);
                    }
                    if (textNode.next_sibling("elision")) {
                        syl->SetCon(sylLog_CON_b);
                    }
                    if (GetContentOfChild(lyric, "syllabic") == "begin") {
                        syl->SetCon(sylLog_CON_d);
                        syl->SetWordpos(sylLog_WORDPOS_i);
                    }
                    else if (GetContentOfChild(lyric, "syllabic") == "middle") {
                        syl->SetCon(sylLog_CON_d);
                        syl->SetWordpos(sylLog_WORDPOS_m);
                    }
                    else if (GetContentOfChild(lyric, "syllabic") == "end") {
                        syl->SetWordpos(sylLog_WORDPOS_t);
                    }
                    if (!textStyle.empty()) syl->SetFontstyle(syl->AttTypography::StrToFontstyle(textStyle.c_str()));
                    if (!textWeight.empty())
                        syl->SetFontweight(syl->AttTypography::StrToFontweight(textWeight.c_str()));

                    Text *text = new Text();
                    text->SetText(UTF8to16(textStr));
                    syl->AddChild(text);
                    verse->AddChild(syl);
                }
            }
            note->AddChild(verse);
        }

        // ties
        pugi::xpath_node startTie = notations.node().select_single_node("tied[@type='start']");
        pugi::xpath_node endTie = notations.node().select_single_node("tied[@type='stop']");
        CloseTie(staff, layer, note, endTie);
        // Then open a new tie
        if ((startTie)) {
            Tie *tie = new Tie();
            // color
            std::string colorStr = GetAttributeValue(startTie.node(), "color");
            if (!colorStr.empty()) tie->SetColor(colorStr.c_str());
            // placement and orientation
            tie->SetCurvedir(ConvertOrientationToCurvedir(GetAttributeValue(startTie.node(), "orientation").c_str()));
            if (!GetAttributeValue(startTie.node(), "placement").empty())
                tie->SetCurvedir(
                    tie->AttCurvature::StrToCurvatureCurvedir(GetAttributeValue(startTie.node(), "placement").c_str()));
            // add it to the stack
            m_controlElements.push_back(std::make_pair(measureNum, tie));
            OpenTie(staff, layer, note, tie);
        }

        // articulation
        std::vector<data_ARTICULATION> artics;
        for (pugi::xml_node articulations = notations.node().child("articulations"); articulations;
             articulations = articulations.next_sibling("articulations")) {
            Artic *artic = new Artic();
            if (articulations.select_single_node("accent")) artics.push_back(ARTICULATION_acc);
            if (articulations.select_single_node("detached-legato")) artics.push_back(ARTICULATION_ten_stacc);
            if (articulations.select_single_node("spiccato")) artics.push_back(ARTICULATION_spicc);
            if (articulations.select_single_node("staccatissimo")) artics.push_back(ARTICULATION_stacciss);
            if (articulations.select_single_node("staccato")) artics.push_back(ARTICULATION_stacc);
            if (articulations.select_single_node("strong-accent")) artics.push_back(ARTICULATION_marc);
            if (articulations.select_single_node("tenuto")) artics.push_back(ARTICULATION_ten);
            artic->SetArtic(artics);
            element->AddChild(artic);
            artics.clear();
        }
        for (pugi::xml_node technical = notations.node().child("technical"); technical;
             technical = technical.next_sibling("technical")) {
            Artic *artic = new Artic();
            if (technical.select_single_node("down-bow")) artics.push_back(ARTICULATION_dnbow);
            if (technical.select_single_node("harmonic")) artics.push_back(ARTICULATION_harm);
            if (technical.select_single_node("open-string")) artics.push_back(ARTICULATION_open);
            if (technical.select_single_node("snap-pizzicato")) artics.push_back(ARTICULATION_snap);
            if (technical.select_single_node("stopped")) artics.push_back(ARTICULATION_stop);
            if (technical.select_single_node("up-bow")) artics.push_back(ARTICULATION_upbow);
            artic->SetArtic(artics);
            artic->SetType("technical");
            element->AddChild(artic);
        }

        // add the note to the layer or to the current container
        AddLayerElement(layer, note);

        // if we are ending a chord remove it from the stack
        if (!nextIsChord) {
            if (!m_elementStack.empty() && m_elementStack.back()->Is(CHORD)) {
                RemoveLastFromStack(CHORD);
            }
        }
    }

    m_ID = "#" + element->GetUuid();

    // Dynamics
    pugi::xpath_node xmlDynam = notations.node().select_single_node("dynamics");
    if (xmlDynam) {
        Dynam *dynam = new Dynam();
        m_controlElements.push_back(std::make_pair(measureNum, dynam));
        dynam->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
        dynam->SetStartid(m_ID);
        std::string dynamStr = GetContentOfChild(xmlDynam.node(), "other-dynamics");
        if (dynamStr.empty()) dynamStr = xmlDynam.node().first_child().name();
        // place
        std::string placeStr = GetAttributeValue(xmlDynam.node(), "placement");
        if (!placeStr.empty()) dynam->SetPlace(dynam->AttPlacement::StrToStaffrel(placeStr.c_str()));
        Text *text = new Text();
        text->SetText(UTF8to16(dynamStr));
        dynam->AddChild(text);
    }

    // fermatas
    pugi::xpath_node xmlFermata = notations.node().select_single_node("fermata");
    if (xmlFermata) {
        Fermata *fermata = new Fermata();
        m_controlElements.push_back(std::make_pair(measureNum, fermata));
        fermata->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
        fermata->SetStartid(m_ID);
        // color
        std::string colorStr = GetAttributeValue(xmlFermata.node(), "color");
        if (!colorStr.empty()) fermata->SetColor(colorStr.c_str());
        // shape
        fermata->SetShape(ConvertFermataShape(GetContent(xmlFermata.node())));
        // form and place
        if (HasAttributeWithValue(xmlFermata.node(), "type", "inverted")) {
            fermata->SetForm(fermataVis_FORM_inv);
            fermata->SetPlace(STAFFREL_below);
        }
        else if (HasAttributeWithValue(xmlFermata.node(), "type", "upright")) {
            fermata->SetForm(fermataVis_FORM_norm);
            fermata->SetPlace(STAFFREL_above);
        }
    }

    // mordent
    pugi::xpath_node xmlMordent = notations.node().select_single_node("ornaments/mordent");
    if (xmlMordent) {
        Mordent *mordent = new Mordent();
        m_controlElements.push_back(std::make_pair(measureNum, mordent));
        mordent->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
        mordent->SetStartid(m_ID);
        // color
        std::string colorStr = GetAttributeValue(xmlMordent.node(), "color");
        if (!colorStr.empty()) mordent->SetColor(colorStr.c_str());
        // form
        mordent->SetForm(mordentLog_FORM_norm);
        // long
        std::string elongation = GetAttributeValue(xmlMordent.node(), "long");
        if (!elongation.empty()) mordent->SetLong(ConvertWordToBool(elongation.c_str()));
        // place
        std::string placeStr = GetAttributeValue(xmlMordent.node(), "placement");
        if (!placeStr.empty()) mordent->SetPlace(mordent->AttPlacement::StrToStaffrel(placeStr.c_str()));
    }
    pugi::xpath_node xmlMordentInv = notations.node().select_single_node("ornaments/inverted-mordent");
    if (xmlMordentInv) {
        Mordent *mordent = new Mordent();
        m_controlElements.push_back(std::make_pair(measureNum, mordent));
        mordent->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
        mordent->SetStartid(m_ID);
        // color
        std::string colorStr = GetAttributeValue(xmlMordentInv.node(), "color");
        if (!colorStr.empty()) mordent->SetColor(colorStr.c_str());
        // form
        mordent->SetForm(mordentLog_FORM_inv);
        // long
        std::string elongation = GetAttributeValue(xmlMordentInv.node(), "long");
        if (!elongation.empty()) mordent->SetLong(ConvertWordToBool(elongation.c_str()));
        // place
        std::string placeStr = GetAttributeValue(xmlMordentInv.node(), "placement");
        if (!placeStr.empty()) mordent->SetPlace(mordent->AttPlacement::StrToStaffrel(placeStr.c_str()));
    }

    // trill
    pugi::xpath_node xmlTrill = notations.node().select_single_node("ornaments/trill-mark");
    if (xmlTrill) {
        Trill *trill = new Trill();
        m_controlElements.push_back(std::make_pair(measureNum, trill));
        trill->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
        trill->SetStartid(m_ID);
        // color
        std::string colorStr = GetAttributeValue(xmlTrill.node(), "color");
        if (!colorStr.empty()) trill->SetColor(colorStr.c_str());
        // place
        std::string placeStr = GetAttributeValue(xmlTrill.node(), "placement");
        if (!placeStr.empty()) trill->SetPlace(trill->AttPlacement::StrToStaffrel(placeStr.c_str()));
    }

    // turn
    pugi::xpath_node xmlTurn = notations.node().select_single_node("ornaments/turn");
    if (xmlTurn) {
        Turn *turn = new Turn();
        m_controlElements.push_back(std::make_pair(measureNum, turn));
        turn->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
        turn->SetStartid(m_ID);
        // color
        std::string colorStr = GetAttributeValue(xmlTurn.node(), "color");
        if (!colorStr.empty()) turn->SetColor(colorStr.c_str());
        // form
        turn->SetForm(turnLog_FORM_norm);
        // place
        std::string placeStr = GetAttributeValue(xmlTurn.node(), "placement");
        if (!placeStr.empty()) turn->SetPlace(turn->AttPlacement::StrToStaffrel(placeStr.c_str()));
    }
    pugi::xpath_node xmlTurnInv = notations.node().select_single_node("ornaments/inverted-turn");
    if (xmlTurnInv) {
        Turn *turn = new Turn();
        m_controlElements.push_back(std::make_pair(measureNum, turn));
        turn->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
        turn->SetStartid(m_ID);
        // color
        std::string colorStr = GetAttributeValue(xmlTurnInv.node(), "color");
        if (!colorStr.empty()) turn->SetColor(colorStr.c_str());
        // form
        turn->SetForm(turnLog_FORM_inv);
        // place
        std::string placeStr = GetAttributeValue(xmlTurnInv.node(), "placement");
        if (!placeStr.empty()) turn->SetPlace(turn->AttPlacement::StrToStaffrel(placeStr.c_str()));
    }

    // slur
    // cross staff slurs won't work
    pugi::xpath_node_set slurs = notations.node().select_nodes("slur");
    for (pugi::xpath_node_set::const_iterator it = slurs.begin(); it != slurs.end(); ++it) {
        pugi::xml_node slur = it->node();
        int slurNumber = atoi(GetAttributeValue(slur, "number").c_str());
        slurNumber = (slurNumber < 1) ? 1 : slurNumber;
        if (HasAttributeWithValue(slur, "type", "start")) {
            Slur *meiSlur = new Slur();
            // color
            std::string colorStr = GetAttributeValue(slur, "color");
            if (!colorStr.empty()) meiSlur->SetColor(colorStr.c_str());
            // lineform
            // meiSlur->SetLform(meiSlur->AttLineVis::StrToLineform(GetAttributeValue(slur, "line-type ").c_str()));
            // placement and orientation
            meiSlur->SetCurvedir(ConvertOrientationToCurvedir(GetAttributeValue(slur, "orientation").c_str()));
            if (!GetAttributeValue(slur, "placement").empty())
                meiSlur->SetCurvedir(
                    meiSlur->AttCurvature::StrToCurvatureCurvedir(GetAttributeValue(slur, "placement").c_str()));
            // add it to the stack
            m_controlElements.push_back(std::make_pair(measureNum, meiSlur));
            OpenSlur(staff, layer, slurNumber, meiSlur);
        }
        else if (HasAttributeWithValue(slur, "type", "stop")) {
            CloseSlur(staff, layer, slurNumber, element);
        }
    }

    // tremolo end
    if (tremolo) {
        if (HasAttributeWithValue(tremolo.node(), "type", "single")) {
            RemoveLastFromStack(BTREM);
        }
        if (HasAttributeWithValue(tremolo.node(), "type", "stop")) {
            RemoveLastFromStack(FTREM);
        }
    }

    // tuplet end
    pugi::xpath_node tupletEnd = notations.node().select_single_node("tuplet[@type='stop']");
    if (tupletEnd) {
        RemoveLastFromStack(TUPLET);
    }

    // beam end
    bool beamEnd = node.select_single_node("beam[@number='1'][text()='end']");
    if (beamEnd) {
        RemoveLastFromStack(BEAM);
    }

    // add StartIDs to dir, dynam, and pedal
    if (!m_dirStack.empty()) {
        std::vector<Dir *>::iterator iter;
        for (iter = m_dirStack.begin(); iter != m_dirStack.end(); iter++) {
            (*iter)->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
            (*iter)->SetStartid(m_ID);
        }
        m_dirStack.clear();
    }
    if (!m_dynamStack.empty()) {
        std::vector<Dynam *>::iterator iter;
        for (iter = m_dynamStack.begin(); iter != m_dynamStack.end(); iter++) {
            (*iter)->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
            (*iter)->SetStartid(m_ID);
        }
        m_dynamStack.clear();
    }
    if (!m_harmStack.empty()) {
        std::vector<Harm *>::iterator iter;
        for (iter = m_harmStack.begin(); iter != m_harmStack.end(); iter++) {
            (*iter)->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
            (*iter)->SetStartid(m_ID);
        }
        m_harmStack.clear();
    }
    if (!m_octaveStack.empty()) {
        std::vector<Octave *>::iterator iter;
        for (iter = m_octaveStack.begin(); iter != m_octaveStack.end(); iter++) {
            (*iter)->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
            (*iter)->SetStartid(m_ID);
        }
        m_octaveStack.clear();
    }
    if (!m_pedalStack.empty()) {
        std::vector<Pedal *>::iterator iter;
        for (iter = m_pedalStack.begin(); iter != m_pedalStack.end(); iter++) {
            (*iter)->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
            (*iter)->SetStartid(m_ID);
        }
        m_pedalStack.clear();
    }
    if (!m_tempoStack.empty()) {
        std::vector<Tempo *>::iterator iter;
        for (iter = m_tempoStack.begin(); iter != m_tempoStack.end(); iter++) {
            (*iter)->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
            (*iter)->SetStartid(m_ID);
        }
        m_tempoStack.clear();
    }
    // add StartID to hairpins
    if (!m_hairpinStack.empty()) {
        std::vector<std::pair<Hairpin *, musicxml::OpenHairpin> >::iterator iter;
        for (iter = m_hairpinStack.begin(); iter != m_hairpinStack.end(); iter++) {
            if (!iter->first->HasStartid()) {
                iter->first->SetStaff(staff->AttCommon::StrToXsdPositiveIntegerList(std::to_string(staff->GetN())));
                iter->first->SetStartid(m_ID);
            }
            iter->second.m_endID = m_ID;
        }
    }
}

void MusicXmlInput::ReadMusicXmlPrint(pugi::xml_node node, Measure *measure, int measureNum)
{
    assert(node);
    assert(measure);

    if (HasAttributeWithValue(node, "new-system", "yes")) {
        // LogWarning("System breaks not supported");
        // enter system break
    }

    if (HasAttributeWithValue(node, "new-page", "yes")) {
        // LogWarning("Page breaks not supported");
        // enter system break
    }
}

//////////////////////////////////////////////////////////////////////////////
// String to attribute converters

data_ACCIDENTAL_EXPLICIT MusicXmlInput::ConvertAccidentalToAccid(std::string value)
{
    if (value == "sharp") return ACCIDENTAL_EXPLICIT_s;
    if (value == "natural") return ACCIDENTAL_EXPLICIT_n;
    if (value == "flat") return ACCIDENTAL_EXPLICIT_f;
    if (value == "double-sharp") return ACCIDENTAL_EXPLICIT_x;
    if (value == "sharp-sharp") return ACCIDENTAL_EXPLICIT_ss;
    if (value == "flat-flat") return ACCIDENTAL_EXPLICIT_ff;
    if (value == "natural-sharp") return ACCIDENTAL_EXPLICIT_ns;
    if (value == "natural-flat") return ACCIDENTAL_EXPLICIT_nf;
    if (value == "quarter-flat") return ACCIDENTAL_EXPLICIT_1qf;
    if (value == "quarter-sharp") return ACCIDENTAL_EXPLICIT_1qs;
    if (value == "three-quarters-flat") return ACCIDENTAL_EXPLICIT_3qf;
    if (value == "three-quarters-sharp") return ACCIDENTAL_EXPLICIT_3qs;
    LogWarning("Unsupported accidental value '%s'", value.c_str());
    return ACCIDENTAL_EXPLICIT_NONE;
}

data_ACCIDENTAL_IMPLICIT MusicXmlInput::ConvertAlterToAccid(float value)
{
    if (value == -2) return ACCIDENTAL_IMPLICIT_ff;
    if (value == -1.5) return ACCIDENTAL_IMPLICIT_fd;
    if (value == -1) return ACCIDENTAL_IMPLICIT_f;
    if (value == -0.5) return ACCIDENTAL_IMPLICIT_fu;
    if (value == 0) return ACCIDENTAL_IMPLICIT_n;
    if (value == 0.5) return ACCIDENTAL_IMPLICIT_sd;
    if (value == 1) return ACCIDENTAL_IMPLICIT_s;
    if (value == 1.5) return ACCIDENTAL_IMPLICIT_su;
    if (value == 2) return ACCIDENTAL_IMPLICIT_ss;
    LogWarning("Unsupported alter value '%d'", value);
    return ACCIDENTAL_IMPLICIT_NONE;
}

data_BARRENDITION MusicXmlInput::ConvertStyleToRend(std::string value, bool repeat)
{
    if (value == "dashed") return BARRENDITION_dashed;
    if (value == "dotted") return BARRENDITION_dotted;
    if (value == "light-light") return BARRENDITION_dbl;
    if (value == "regular") return BARRENDITION_dbldashed;
    if (value == "regular") return BARRENDITION_dbldotted;
    if ((value == "light-heavy") && !repeat) return BARRENDITION_end;
    if (value == "none") return BARRENDITION_invis;
    if ((value == "heavy-light") && repeat) return BARRENDITION_rptstart;
    // if (value == "") return BARRENDITION_rptboth;
    if ((value == "light-heavy") && repeat) return BARRENDITION_rptend;
    if (value == "regular") return BARRENDITION_single;
    LogWarning("Unsupported bar-style '%s'", value.c_str());
    return BARRENDITION_NONE;
}

data_BOOLEAN MusicXmlInput::ConvertWordToBool(std::string value)
{
    if (value == "yes") return BOOLEAN_true;
    if (value == "no") return BOOLEAN_false;
    return BOOLEAN_NONE;
}

data_DURATION MusicXmlInput::ConvertTypeToDur(std::string value)
{
    if (value == "maxima") return DURATION_maxima; // this is a mensural MEI value
    if (value == "long") return DURATION_long; // mensural MEI value longa isn't supported
    if (value == "breve") return DURATION_breve;
    if (value == "whole") return DURATION_1;
    if (value == "half") return DURATION_2;
    if (value == "quarter") return DURATION_4;
    if (value == "eighth") return DURATION_8;
    if (value == "16th") return DURATION_16;
    if (value == "32nd") return DURATION_32;
    if (value == "64th") return DURATION_64;
    if (value == "128th") return DURATION_128;
    if (value == "256th") return DURATION_256;
    LogWarning("Unsupported type '%s'", value.c_str());
    return DURATION_NONE;
}

data_PITCHNAME MusicXmlInput::ConvertStepToPitchName(std::string value)
{
    if (value == "C") return PITCHNAME_c;
    if (value == "D") return PITCHNAME_d;
    if (value == "E") return PITCHNAME_e;
    if (value == "F") return PITCHNAME_f;
    if (value == "G") return PITCHNAME_g;
    if (value == "A") return PITCHNAME_a;
    if (value == "B") return PITCHNAME_b;
    LogWarning("Unsupported pitch name '%s'", value.c_str());
    return PITCHNAME_NONE;
}

curvature_CURVEDIR MusicXmlInput::ConvertOrientationToCurvedir(std::string value)
{
    if (value == "over") return curvature_CURVEDIR_above;
    if (value == "under") return curvature_CURVEDIR_below;
    return curvature_CURVEDIR_NONE;
}

fermataVis_SHAPE MusicXmlInput::ConvertFermataShape(std::string value)
{
    if (value == "normal") return fermataVis_SHAPE_curved;
    if (value == "angled") return fermataVis_SHAPE_angular;
    if (value == "square") return fermataVis_SHAPE_square;
    return fermataVis_SHAPE_NONE;
}

pedalLog_DIR MusicXmlInput::ConvertPedalTypeToDir(std::string value)
{
    if (value == "start") return pedalLog_DIR_down;
    if (value == "stop") return pedalLog_DIR_up;
    LogWarning("Unsupported type '%s' for pedal", value.c_str());
    return pedalLog_DIR_NONE;
}

tupletVis_NUMFORMAT MusicXmlInput::ConvertTupletNumberValue(std::string value)
{
    if (value == "actual") return tupletVis_NUMFORMAT_count;
    if (value == "both") return tupletVis_NUMFORMAT_ratio;
    return tupletVis_NUMFORMAT_NONE;
}

} // namespace vrv
