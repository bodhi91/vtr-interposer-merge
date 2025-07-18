/* The XML parser processes an XML file into a tree data structure composed of
 * pugi::xml_nodes.  Each node represents an XML element.  For example
 * <a> <b/> </a> will generate two pugi::xml_nodes.  One called "a" and its
 * child "b".  Each pugi::xml_node can contain various XML data such as attribute
 * information and text content.  The XML parser provides several functions to
 * help the developer build, and traverse tree (this is also sometimes referred to
 * as the Document Object Model or DOM).
 *
 * For convenience, it often makes sense to use some wraper functions (provided in
 * the pugiutil namespace of libvtrutil) which simplify loading an XML file and
 * error handling.
 *
 * The function pugiutil::load_xml() reads in an xml file.
 *
 * The function pugiutil::get_single_child() returns a child xml_node for a given parent
 * xml_node if there is a child which matches the name provided by the developer.
 *
 * The function pugiutil::get_attribute() is used to extract attributes from an
 * xml_node, returning a pugi::xml_attribute. xml_attribute objects support accessors
 * such as as_float(), as_int() to retrieve semantic values. See pugixml documentation
 * for more details.
 *
 * Architecture file checks already implemented (Daniel Chen):
 *		- Duplicate pb_types, pb_type ports, models, model ports,
 *			interconnects, interconnect annotations.
 *		- Port and pin range checking (port with 4 pins can only be
 *			accessed within [0:3].
 *		- LUT delay matrix size matches # of LUT inputs
 *		- Ensures XML tags are ordered.
 *		- Clocked primitives that have timing annotations must have a clock
 *			name matching the primitive.
 *		- Enforced VPR definition of LUT and FF must have one input port (n pins)
 *			and one output port(1 pin).
 *		- Checks file extension for blif and architecture xml file, avoid crashes if
 *			the two files are swapped on command line.
 *
 */

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <algorithm>

#include "pugixml.hpp"
#include "pugixml_util.hpp"

#include "vtr_assert.h"
#include "vtr_log.h"
#include "vtr_util.h"
#include "vtr_memory.h"
#include "vtr_digest.h"
#include "vtr_token.h"
#include "vtr_bimap.h"

#include "arch_check.h"
#include "arch_error.h"
#include "arch_util.h"
#include "arch_types.h"

#include "read_xml_arch_file.h"
#include "read_xml_util.h"
#include "parse_switchblocks.h"

#include "physical_types_util.h"
#include "vtr_expr_eval.h"

#include "read_xml_arch_file_noc_tag.h"

using namespace std::string_literals;
using pugiutil::ReqOpt;

struct t_fc_override {
    std::string port_name;
    std::string seg_name;
    e_fc_value_type fc_value_type;
    float fc_value;
};

struct t_pin_counts {
    int input = 0;
    int output = 0;
    int clock = 0;

    int total() const {
        return input + output + clock;
    }
};

struct t_pin_locs {
  private:
    // Distribution must be set once for each physical tile type
    // and must be equal for each sub tile within a physical tile.
    bool distribution_set = false;

  public:
    e_pin_location_distr distribution = e_pin_location_distr::SPREAD;

    /* [0..num_sub_tiles-1][0..width-1][0..height-1][0..num_of_layer-1][0..3][0..num_tokens-1] */
    vtr::NdMatrix<std::vector<std::string>, 5> assignments;

    bool is_distribution_set() const {
        return distribution_set;
    }

    void set_distribution() {
        VTR_ASSERT(distribution_set == false);
        distribution_set = true;
    }
};

/* Function prototypes */
/*   Populate data */

static void LoadPinLoc(pugi::xml_node Locations,
                       t_physical_tile_type* type,
                       t_pin_locs* pin_locs,
                       const pugiutil::loc_data& loc_data,
                       const int num_of_avail_layer);
template<typename T>
static std::pair<int, int> ProcessPinString(pugi::xml_node Locations,
                                            T type,
                                            const char* pin_loc_string,
                                            const pugiutil::loc_data& loc_data);

/* Process XML hierarchy */
static void ProcessTiles(pugi::xml_node Node,
                         std::vector<t_physical_tile_type>& PhysicalTileTypes,
                         std::vector<t_logical_block_type>& LogicalBlockTypes,
                         const t_default_fc_spec& arch_def_fc,
                         t_arch& arch,
                         const pugiutil::loc_data& loc_data,
                         int num_of_avail_layer);

// TODO: Remove block_type_contains_blif_model / pb_type_contains_blif_model
// as part of
// https://github.com/verilog-to-routing/vtr-verilog-to-routing/issues/1193
static void MarkIoTypes(std::vector<t_physical_tile_type>& PhysicalTileTypes);

static void ProcessTileProps(pugi::xml_node Node,
                             t_physical_tile_type* PhysicalTileType,
                             const pugiutil::loc_data& loc_data);

static t_pin_counts ProcessSubTilePorts(pugi::xml_node Parent,
                                        t_sub_tile* SubTile,
                                        const pugiutil::loc_data& loc_data);

static void ProcessTilePort(pugi::xml_node Node,
                            t_physical_tile_port* port,
                            const pugiutil::loc_data& loc_data);

static void ProcessTileEquivalentSites(pugi::xml_node Parent,
                                       t_sub_tile* SubTile,
                                       t_physical_tile_type* PhysicalTileType,
                                       std::vector<t_logical_block_type>& LogicalBlockTypes,
                                       const pugiutil::loc_data& loc_data);
static void ProcessEquivalentSiteDirectConnection(pugi::xml_node Parent,
                                                  t_sub_tile* SubTile,
                                                  t_physical_tile_type* PhysicalTileType,
                                                  t_logical_block_type* LogicalBlockType,
                                                  const pugiutil::loc_data& loc_data);
static void ProcessEquivalentSiteCustomConnection(pugi::xml_node Parent,
                                                  t_sub_tile* SubTile,
                                                  t_physical_tile_type* PhysicalTileType,
                                                  t_logical_block_type* LogicalBlockType,
                                                  const std::string& site_name,
                                                  const pugiutil::loc_data& loc_data);
static void ProcessPinLocations(pugi::xml_node Locations,
                                t_physical_tile_type* PhysicalTileType,
                                t_sub_tile* SubTile,
                                t_pin_locs* pin_locs,
                                const pugiutil::loc_data& loc_data,
                                const int num_of_avail_layer);

static void ProcessSubTiles(pugi::xml_node Node,
                            t_physical_tile_type* PhysicalTileType,
                            std::vector<t_logical_block_type>& LogicalBlockTypes,
                            std::vector<t_segment_inf>& segments,
                            const t_default_fc_spec& arch_def_fc,
                            const pugiutil::loc_data& loc_data,
                            const int num_of_avail_layer);

/**
 * @brief Parses a <pb_type> tag and all <mode> and <pb_type> tags under it.
 *
 * @param Parent An XML node pointing to <pb_type> tag to be processed
 * @param pb_type To be filled by this function with parsed information
 * about the given pb_type
 * @param mode The parent mode of the pb_type to be processed. If the given
 * pb_type is the root, nullptr should be passed.
 * @param timing_enabled Determines whether timing-aware optimizations are enabled.
 * @param arch Contains high-level architecture information like models and
 * string interment storage.
 * @param loc_data Points to the location in the architecture file where the parser is reading.
 * @param pb_idx Used to assign unique values to index_in_logical_block field in
 * t_pb_type for all pb_types under a logical block type.
 */
static void ProcessPb_Type(pugi::xml_node Parent,
                           t_pb_type* pb_type,
                           t_mode* mode,
                           bool timing_enabled,
                           const t_arch& arch,
                           const pugiutil::loc_data& loc_data,
                           int& pb_idx);

static void ProcessPb_TypePort(pugi::xml_node Parent,
                               t_port* port,
                               e_power_estimation_method power_method,
                               const bool is_root_pb_type,
                               const pugiutil::loc_data& loc_data);
static void ProcessPinToPinAnnotations(pugi::xml_node parent,
                                       t_pin_to_pin_annotation* annotation,
                                       t_pb_type* parent_pb_type,
                                       const pugiutil::loc_data& loc_data);

/**
 * @brief Parses <interconnect> tags under a <mode> or <pb_type> tag.
 *
 * @param strings String internment storage used to store strings used
 * as keys and values in <metadata> tags under the given <interconnect> tag.
 * @param Parent An XML node pointing to <interconnect> tag to be processed
 * @param mode To be filled with interconnect-related information.
 * @param loc_data Points to the location in the architecture file where
 * the parser is reading.
 */
static void ProcessInterconnect(vtr::string_internment& strings,
                                pugi::xml_node Parent,
                                t_mode* mode,
                                const pugiutil::loc_data& loc_data);

/**
 * @brief Processes a <mode> tag under <pb_type> tag in the architecture file.
 * If a <pb_type> tag does not have any <mode> tags, a default mode is implied.
 *
 * @param Parent An XML node referring to either <mode> tag or <pb_type> tag.
 * It the XML node refers to <pb_type> tag, the mode is implied.
 * @param mode Mode information to be filled by this function.
 * @param timing_enabled Determines whether timing-aware optimizations are enabled.
 * @param arch Contains high-level architecture information like models and
 * string interment storage.
 * @param loc_data Points to the location in the architecture file where the parser is reading.
 * @param parent_pb_idx Used to assign unique values to index_in_logical_block field in
 * t_pb_type for all pb_types under a logical block type.
 */
static void ProcessMode(pugi::xml_node Parent,
                        t_mode* mode,
                        bool timing_enabled,
                        const t_arch& arch,
                        const pugiutil::loc_data& loc_data,
                        int& parent_pb_idx);
/**
 * @brief Processes <metadata> tags.
 *
 * @param strings String internment storage used to store strings used
 * as keys and values in <metadata> tags.
 * @param Parent An XML node pointing to the parent tag whose <metadata> children
 * are to be parsed.
 * @param loc_data Points to the location in the architecture file where the parser is reading.
 * @return A t_metadata_dict that stored parsed (key, value) pairs.
 */
static t_metadata_dict ProcessMetadata(vtr::string_internment& strings,
                                       pugi::xml_node Parent,
                                       const pugiutil::loc_data& loc_data);

static void Process_Fc_Values(pugi::xml_node Node, t_default_fc_spec& spec, const pugiutil::loc_data& loc_data);
static void Process_Fc(pugi::xml_node Node,
                       t_physical_tile_type* PhysicalTileType,
                       t_sub_tile* SubTile,
                       t_pin_counts pin_counts,
                       std::vector<t_segment_inf>& segments,
                       const t_default_fc_spec& arch_def_fc,
                       const pugiutil::loc_data& loc_data);
static t_fc_override Process_Fc_override(pugi::xml_node node, const pugiutil::loc_data& loc_data);

/**
 * @brief Processes optional <switchblock_locations> tag under a <tile> tag//
 *
 * @param switchblock_locations An XML node pointing to <switchblock_locations> tag
 * if it exists.
 * @param type To be filled with information extracted from the given
 * <switchblock_locations> tag. This function fills switchblock_locations and
 * switchblock_switch_overrides fields.
 * @param arch Used to find switchblock by name
 * @param loc_data Points to the location in the xml file where the parser is reading.
 */
static void ProcessSwitchblockLocations(pugi::xml_node switchblock_locations,
                                        t_physical_tile_type* type,
                                        const t_arch& arch,
                                        const pugiutil::loc_data& loc_data);

static e_fc_value_type string_to_fc_value_type(const std::string& str, pugi::xml_node node, const pugiutil::loc_data& loc_data);
static void ProcessChanWidthDistr(pugi::xml_node Node,
                                  t_arch* arch,
                                  const pugiutil::loc_data& loc_data);
static void ProcessChanWidthDistrDir(pugi::xml_node Node, t_chan* chan, const pugiutil::loc_data& loc_data);
static void ProcessModels(pugi::xml_node Node, t_arch* arch, const pugiutil::loc_data& loc_data);
static void ProcessModelPorts(pugi::xml_node port_group, t_model* model, std::set<std::string>& port_names, const pugiutil::loc_data& loc_data);
static void ProcessLayout(pugi::xml_node Node, t_arch* arch, const pugiutil::loc_data& loc_data, int& num_of_avail_layer);
static t_grid_def ProcessGridLayout(vtr::string_internment& strings, pugi::xml_node layout_type_tag, const pugiutil::loc_data& loc_data, t_arch* arch, int& num_of_avail_layer);
static void ProcessBlockTypeLocs(t_grid_def& grid_def, int die_number, vtr::string_internment& strings, pugi::xml_node layout_block_type_tag, const pugiutil::loc_data& loc_data);
static int get_number_of_layers(pugi::xml_node layout_type_tag, const pugiutil::loc_data& loc_data);
static void ProcessDevice(pugi::xml_node Node, t_arch* arch, t_default_fc_spec& arch_def_fc, const pugiutil::loc_data& loc_data);

/**
 * @brief Parses <complexblocklist> tag in the architecture file.
 *
 * @param Node The xml node referring to <complexblocklist> tag
 * @param LogicalBlockTypes This function fills this vector with all available
 * logical block types.
 * @param arch Used to access models and string internment storage.
 * @param timing_enabled Determines whether timing-aware optimizations are enabled.
 * @param loc_data Points to the location in the xml file where the parser is reading.
 */
static void ProcessComplexBlocks(pugi::xml_node Node,
                                 std::vector<t_logical_block_type>& LogicalBlockTypes,
                                 const t_arch& arch,
                                 bool timing_enabled,
                                 const pugiutil::loc_data& loc_data);

static std::vector<t_arch_switch_inf> ProcessSwitches(pugi::xml_node Node,
                                                      const bool timing_enabled,
                                                      const pugiutil::loc_data& loc_data);

static void ProcessSwitchTdel(pugi::xml_node Node, const bool timing_enabled, t_arch_switch_inf& arch_switch, const pugiutil::loc_data& loc_data);

static std::vector<t_direct_inf> ProcessDirects(pugi::xml_node Parent,
                                                const std::vector<t_arch_switch_inf>& switches,
                                                const pugiutil::loc_data& loc_data);

static void ProcessClockMetalLayers(pugi::xml_node parent,
                                    std::unordered_map<std::string, t_metal_layer>& metal_layers,
                                    pugiutil::loc_data& loc_data);
static void ProcessClockNetworks(pugi::xml_node parent,
                                 std::vector<t_clock_network_arch>& clock_networks,
                                 const std::vector<t_arch_switch_inf>& switches,
                                 pugiutil::loc_data& loc_data);
static void ProcessClockSwitchPoints(pugi::xml_node parent,
                                     t_clock_network_arch& clock_network,
                                     const std::vector<t_arch_switch_inf>& switches,
                                     pugiutil::loc_data& loc_data);
static void ProcessClockRouting(pugi::xml_node parent,
                                std::vector<t_clock_connection_arch>& clock_connections,
                                const std::vector<t_arch_switch_inf>& switches,
                                pugiutil::loc_data& loc_data);

static std::vector<t_segment_inf> ProcessSegments(pugi::xml_node Parent,
                                                  const std::vector<t_arch_switch_inf>& switches,
                                                  const bool timing_enabled,
                                                  const bool switchblocklist_required,
                                                  const pugiutil::loc_data& loc_data);

static void ProcessSwitchblocks(pugi::xml_node Parent, t_arch* arch, const pugiutil::loc_data& loc_data);
static void ProcessCB_SB(pugi::xml_node Node, std::vector<bool>& list, const pugiutil::loc_data& loc_data);
static void ProcessPower(pugi::xml_node parent,
                         t_power_arch* power_arch,
                         const pugiutil::loc_data& loc_data);

static void ProcessClocks(pugi::xml_node Parent, t_clock_arch* clocks, const pugiutil::loc_data& loc_data);

static void ProcessPb_TypePowerEstMethod(pugi::xml_node Parent, t_pb_type* pb_type, const pugiutil::loc_data& loc_data);
static void ProcessPb_TypePort_Power(pugi::xml_node Parent, t_port* port, e_power_estimation_method power_method, const pugiutil::loc_data& loc_data);

std::string inst_port_to_port_name(std::string inst_port);

static bool attribute_to_bool(const pugi::xml_node node,
                              const pugi::xml_attribute attr,
                              const pugiutil::loc_data& loc_data);

/**
 * @brief Searches for a switch whose matches with the given name.
 * @param switches Contains all the architecture switches.
 * @param switch_name The name with which switch names are compared.
 * @return A negative integer if no switch was found with the given name; otherwise
 * the index of the matching switch is returned.
 */
static int find_switch_by_name(const std::vector<t_arch_switch_inf>& switches, std::string_view switch_name);

static e_side string_to_side(const std::string& side_str);

template<typename T>
static T* get_type_by_name(std::string_view type_name, std::vector<T>& types);

#ifdef INTERPOSER_BASED_ARCHITECTURE
/*
 * Interposer-related utility functions
 * These functions are used to calculate the least common multiple (LCM) of block heights
 * to ensure that interposer cuts don't go through physical blocks.
 */

/**
 * @brief Computes the greatest common divisor of two integers using Euclidean algorithm
 * @param a First integer
 * @param b Second integer  
 * @return Greatest common divisor of a and b
 */
static int gcd(int a, int b) {
    if (b == 0)
        return a;
    else
        return gcd(b, a % b);
}

/**
 * @brief Computes the least common multiple of two integers
 * @param a First integer
 * @param b Second integer
 * @return Least common multiple of a and b, or -1 if GCD is 0
 */
static int lcm(int a, int b) {
    int mygcd = gcd(a, b);

    if (mygcd == 0) {
        return -1;
    } else {
        return (a * b) / mygcd;
    }
}
#endif

/*
 *
 *
 * External Function Implementations
 *
 *
 */

/* Loads the given architecture file. */
void XmlReadArch(const char* ArchFile,
                 const bool timing_enabled,
                 t_arch* arch,
                 std::vector<t_physical_tile_type>& PhysicalTileTypes,
                 std::vector<t_logical_block_type>& LogicalBlockTypes) {
    pugi::xml_node Next;
    ReqOpt POWER_REQD, SWITCHBLOCKLIST_REQD;

    if (!vtr::check_file_name_extension(ArchFile, ".xml")) {
        VTR_LOG_WARN(
            "Architecture file '%s' may be in incorrect format. "
            "Expecting .xml format for architecture files.\n",
            ArchFile);
    }

    //Create a unique identifier for this architecture file based on it's contents
    arch->architecture_id = vtr::strdup(vtr::secure_digest_file(ArchFile).c_str());

    /* Parse the file */
    pugi::xml_document doc;
    pugiutil::loc_data loc_data;
    t_default_fc_spec arch_def_fc;
    try {
        loc_data = pugiutil::load_xml(doc, ArchFile);

        set_arch_file_name(ArchFile);

        /* Root node should be architecture */
        auto architecture = get_single_child(doc, "architecture", loc_data);

        /* TODO: do version processing properly with string delimiting on the . */
#if 0
        char* Prop = get_attribute(architecture, "version", loc_data, ReqOpt::OPTIONAL).as_string(NULL);
        if (Prop != NULL) {
            if (atof(Prop) > atof(VPR_VERSION)) {
                VTR_LOG_WARN( "This architecture version is for VPR %f while your current VPR version is " VPR_VERSION ", compatability issues may arise\n",
                        atof(Prop));
            }
        }
#endif

        /* Process models */
        Next = get_single_child(architecture, "models", loc_data);
        ProcessModels(Next, arch, loc_data);
        CreateModelLibrary(arch);

        /* Process layout */
        int num_of_avail_layers = 0;
        Next = get_single_child(architecture, "layout", loc_data);
        ProcessLayout(Next, arch, loc_data, num_of_avail_layers);

        /* Process device */
        Next = get_single_child(architecture, "device", loc_data);
        ProcessDevice(Next, arch, arch_def_fc, loc_data);

        /* Process switches */
        Next = get_single_child(architecture, "switchlist", loc_data);
        arch->switches = ProcessSwitches(Next, timing_enabled, loc_data);

        /* Process switchblocks. This depends on switches */
        bool switchblocklist_required = (arch->SBType == CUSTOM); //require this section only if custom switchblocks are used
        SWITCHBLOCKLIST_REQD = BoolToReqOpt(switchblocklist_required);

        /* Process segments. This depends on switches */
        Next = get_single_child(architecture, "segmentlist", loc_data);
        arch->Segments = ProcessSegments(Next, arch->switches, timing_enabled, switchblocklist_required, loc_data);

        Next = get_single_child(architecture, "switchblocklist", loc_data, SWITCHBLOCKLIST_REQD);
        if (Next) {
            ProcessSwitchblocks(Next, arch, loc_data);
        }

        /* Process logical block types */
        Next = get_single_child(architecture, "complexblocklist", loc_data);
        ProcessComplexBlocks(Next, LogicalBlockTypes, *arch, timing_enabled, loc_data);

        /* Process logical block types */
        Next = get_single_child(architecture, "tiles", loc_data);
        ProcessTiles(Next, PhysicalTileTypes, LogicalBlockTypes, arch_def_fc, *arch, loc_data, num_of_avail_layers);

#ifdef INTERPOSER_BASED_ARCHITECTURE
        /* Calculate the least common multiple of block heights for interposer-based architectures
         * This ensures that cutlines do not go through physical blocks */
        arch->lcm_of_block_heights = 1;
        for (const auto& PhysicalTileType : PhysicalTileTypes) {
            if (PhysicalTileType.height > 0) {
                arch->lcm_of_block_heights = lcm(arch->lcm_of_block_heights, PhysicalTileType.height);
                if (arch->lcm_of_block_heights < 0) {
                    archfpga_throw(ArchFile, 0,
                                   "Error calculating LCM of block heights for interposer architecture. "
                                   "Block height calculation resulted in invalid LCM.\n");
                }
            }
        }
        VTR_LOG("Interposer Architecture: LCM of block heights = %d\n", arch->lcm_of_block_heights);
#endif

        /* Link Physical Tiles with Logical Blocks */
        link_physical_logical_types(PhysicalTileTypes, LogicalBlockTypes);

        /* Process directs */
        Next = get_single_child(architecture, "directlist", loc_data, ReqOpt::OPTIONAL);
        if (Next) {
            arch->directs = ProcessDirects(Next, arch->switches, loc_data);
        }

        /* Process Clock Networks */
        Next = get_single_child(architecture, "clocknetworks", loc_data, ReqOpt::OPTIONAL);
        if (Next) {
            std::vector<std::string> expected_children = {"metal_layers", "clock_network", "clock_routing"};
            expect_only_children(Next, expected_children, loc_data);

            ProcessClockMetalLayers(Next, arch->clock_arch.clock_metal_layers, loc_data);

            ProcessClockNetworks(Next,
                                 arch->clock_arch.clock_networks_arch,
                                 arch->switches,
                                 loc_data);

            ProcessClockRouting(Next,
                                arch->clock_arch.clock_connections_arch,
                                arch->switches,
                                loc_data);
        }

        /* Process architecture power information */

        /* If arch->power has been initialized, meaning the user has requested power estimation,
         * then the power architecture information is required.
         */
        if (arch->power) {
            POWER_REQD = ReqOpt::REQUIRED;
        } else {
            POWER_REQD = ReqOpt::OPTIONAL;
        }

        Next = get_single_child(architecture, "power", loc_data, POWER_REQD);
        if (Next) {
            if (arch->power) {
                ProcessPower(Next, arch->power, loc_data);
            } else {
                /* This information still needs to be read, even if it is just
                 * thrown away.
                 */
                t_power_arch* power_arch_fake = (t_power_arch*)vtr::calloc(1,
                                                                           sizeof(t_power_arch));
                ProcessPower(Next, power_arch_fake, loc_data);
                free(power_arch_fake);
            }
        }

        // Process Clocks
        Next = get_single_child(architecture, "clocks", loc_data, POWER_REQD);
        if (Next) {
            if (arch->clocks) {
                ProcessClocks(Next, arch->clocks, loc_data);
            } else {
                /* This information still needs to be read, even if it is just
                 * thrown away.
                 */
                t_clock_arch* clocks_fake = (t_clock_arch*)vtr::calloc(1,
                                                                       sizeof(t_clock_arch));
                ProcessClocks(Next, clocks_fake, loc_data);
                free(clocks_fake->clock_inf);
                free(clocks_fake);
            }
        }

        // process NoC (optional)
        Next = get_single_child(architecture, "noc", loc_data, pugiutil::OPTIONAL);
        if (Next) {
            process_noc_tag(Next, arch, loc_data);
        }

        SyncModelsPbTypes(arch, LogicalBlockTypes);
        check_models(arch);

        MarkIoTypes(PhysicalTileTypes);
    } catch (pugiutil::XmlError& e) {
        archfpga_throw(ArchFile, e.line(),
                       "%s", e.what());
    }
}

/*
 *
 *
 * File-scope function implementations
 *
 *
 */

static void LoadPinLoc(pugi::xml_node Locations,
                       t_physical_tile_type* type,
                       t_pin_locs* pin_locs,
                       const pugiutil::loc_data& loc_data,
                       const int num_of_avail_layer) {
    type->pin_width_offset.resize(type->num_pins, 0);
    type->pin_height_offset.resize(type->num_pins, 0);
    //layer_offset is not used if the distribution is not custom
    type->pin_layer_offset.resize(type->num_pins, 0);

    std::vector<int> physical_pin_counts(type->num_pins, 0);
    if (pin_locs->distribution == e_pin_location_distr::SPREAD) {
        /* evenly distribute pins starting at bottom left corner */

        int num_sides = 4 * (type->width * type->height);
        int side_index = 0;
        int count = 0;
        for (e_side side : TOTAL_2D_SIDES) {
            for (int width = 0; width < type->width; ++width) {
                for (int height = 0; height < type->height; ++height) {
                    for (int pin_offset = 0; pin_offset < (type->num_pins / num_sides) + 1; ++pin_offset) {
                        int pin_num = side_index + pin_offset * num_sides;
                        if (pin_num < type->num_pins) {
                            type->pinloc[width][height][side][pin_num] = true;
                            type->pin_width_offset[pin_num] += width;
                            type->pin_height_offset[pin_num] += height;
                            physical_pin_counts[pin_num] += 1;
                            count++;
                        }
                    }
                    side_index++;
                }
            }
        }
        VTR_ASSERT(side_index == num_sides);
        VTR_ASSERT(count == type->num_pins);
    } else if (pin_locs->distribution == e_pin_location_distr::PERIMETER) {
        //Add one pin at-a-time to perimeter sides in round-robin order
        int ipin = 0;
        while (ipin < type->num_pins) {
            for (int width = 0; width < type->width; ++width) {
                for (int height = 0; height < type->height; ++height) {
                    for (e_side side : TOTAL_2D_SIDES) {
                        if (((width == 0 && side == LEFT)
                             || (height == type->height - 1 && side == TOP)
                             || (width == type->width - 1 && side == RIGHT)
                             || (height == 0 && side == BOTTOM))
                            && ipin < type->num_pins) {
                            //On a side, with pins still to allocate

                            type->pinloc[width][height][side][ipin] = true;
                            type->pin_width_offset[ipin] += width;
                            type->pin_height_offset[ipin] += height;
                            physical_pin_counts[ipin] += 1;
                            ++ipin;
                        }
                    }
                }
            }
        }
        VTR_ASSERT(ipin == type->num_pins);

    } else if (pin_locs->distribution == e_pin_location_distr::SPREAD_INPUTS_PERIMETER_OUTPUTS) {
        //Collect the sets of block input/output pins
        std::vector<int> input_pins;
        std::vector<int> output_pins;
        for (int pin_num = 0; pin_num < type->num_pins; ++pin_num) {
            auto class_type = get_pin_type_from_pin_physical_num(type, pin_num);

            if (class_type == RECEIVER) {
                input_pins.push_back(pin_num);
            } else {
                VTR_ASSERT(class_type == DRIVER);
                output_pins.push_back(pin_num);
            }
        }

        //Allocate the inputs one pin at-a-time in a round-robin order
        //to all sides
        size_t ipin = 0;
        while (ipin < input_pins.size()) {
            for (int width = 0; width < type->width; ++width) {
                for (int height = 0; height < type->height; ++height) {
                    for (e_side side : TOTAL_2D_SIDES) {
                        if (ipin < input_pins.size()) {
                            //Pins still to allocate

                            int pin_num = input_pins[ipin];

                            type->pinloc[width][height][side][pin_num] = true;
                            type->pin_width_offset[pin_num] += width;
                            type->pin_height_offset[pin_num] += height;
                            physical_pin_counts[pin_num] += 1;
                            ++ipin;
                        }
                    }
                }
            }
        }
        VTR_ASSERT(ipin == input_pins.size());

        //Allocate the outputs one pin at-a-time to perimeter sides in round-robin order
        ipin = 0;
        while (ipin < output_pins.size()) {
            for (int width = 0; width < type->width; ++width) {
                for (int height = 0; height < type->height; ++height) {
                    for (e_side side : TOTAL_2D_SIDES) {
                        if (((width == 0 && side == LEFT)
                             || (height == type->height - 1 && side == TOP)
                             || (width == type->width - 1 && side == RIGHT)
                             || (height == 0 && side == BOTTOM))
                            && ipin < output_pins.size()) {
                            //On a perimeter side, with pins still to allocate

                            int pin_num = output_pins[ipin];

                            type->pinloc[width][height][side][pin_num] = true;
                            type->pin_width_offset[pin_num] += width;
                            type->pin_height_offset[pin_num] += height;
                            physical_pin_counts[pin_num] += 1;
                            ++ipin;
                        }
                    }
                }
            }
        }
        VTR_ASSERT(ipin == output_pins.size());

    } else {
        VTR_ASSERT(pin_locs->distribution == e_pin_location_distr::CUSTOM);
        for (auto& sub_tile : type->sub_tiles) {
            int sub_tile_index = sub_tile.index;
            int sub_tile_capacity = sub_tile.capacity.total();

            for (int layer = 0; layer < num_of_avail_layer; ++layer) {
                for (int width = 0; width < type->width; ++width) {
                    for (int height = 0; height < type->height; ++height) {
                        for (e_side side : TOTAL_2D_SIDES) {
                            for (auto token : pin_locs->assignments[sub_tile_index][width][height][layer][side]) {
                                auto pin_range = ProcessPinString<t_sub_tile*>(Locations,
                                                                               &sub_tile,
                                                                               token.c_str(),
                                                                               loc_data);

                                for (int pin_num = pin_range.first; pin_num < pin_range.second; ++pin_num) {
                                    VTR_ASSERT(pin_num < (int)sub_tile.sub_tile_to_tile_pin_indices.size() / sub_tile_capacity);
                                    for (int capacity = 0; capacity < sub_tile_capacity; ++capacity) {
                                        int sub_tile_pin_index = pin_num + capacity * sub_tile.num_phy_pins / sub_tile_capacity;
                                        int physical_pin_index = sub_tile.sub_tile_to_tile_pin_indices[sub_tile_pin_index];
                                        type->pinloc[width][height][side][physical_pin_index] = true;
                                        type->pin_width_offset[physical_pin_index] += width;
                                        type->pin_height_offset[physical_pin_index] += height;
                                        type->pin_layer_offset[physical_pin_index] = layer;
                                        physical_pin_counts[physical_pin_index] += 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (int ipin = 0; ipin < type->num_pins; ++ipin) {
        VTR_ASSERT(physical_pin_counts[ipin] >= 1);

        type->pin_width_offset[ipin] /= physical_pin_counts[ipin];
        type->pin_height_offset[ipin] /= physical_pin_counts[ipin];

        VTR_ASSERT(type->pin_width_offset[ipin] >= 0 && type->pin_width_offset[ipin] < type->width);
        VTR_ASSERT(type->pin_height_offset[ipin] >= 0 && type->pin_height_offset[ipin] < type->height);
        VTR_ASSERT(type->pin_layer_offset[ipin] >= 0 && type->pin_layer_offset[ipin] < num_of_avail_layer);
    }
}

template<typename T>
static std::pair<int, int> ProcessPinString(pugi::xml_node Locations,
                                            T type,
                                            const char* pin_loc_string,
                                            const pugiutil::loc_data& loc_data) {
    int num_tokens;
    auto tokens = GetTokensFromString(pin_loc_string, &num_tokens);

    int token_index = 0;
    auto token = tokens[token_index];

    if (token.type != TOKEN_STRING || token.data != type->name) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                       "Wrong physical type name of the port: %s\n", pin_loc_string);
    }

    token_index++;
    token = tokens[token_index];

    if (token.type != TOKEN_DOT) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                       "No dot is present to separate type name and port name: %s\n", pin_loc_string);
    }

    token_index++;
    token = tokens[token_index];

    if (token.type != TOKEN_STRING) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                       "No port name is present: %s\n", pin_loc_string);
    }

    auto port = type->get_port(token.data);
    if (port == nullptr) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                       "Port %s for %s could not be found: %s\n",
                       type->name.c_str(), token.data,
                       pin_loc_string);
    }
    int abs_first_pin_idx = port->absolute_first_pin_index;

    token_index++;

    // All the pins of the port are taken or the port has a single pin
    if (token_index == num_tokens) {
        freeTokens(tokens, num_tokens);
        return std::make_pair(abs_first_pin_idx, abs_first_pin_idx + port->num_pins);
    }

    token = tokens[token_index];

    if (token.type != TOKEN_OPEN_SQUARE_BRACKET) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                       "No open square bracket present: %s\n", pin_loc_string);
    }

    token_index++;
    token = tokens[token_index];

    if (token.type != TOKEN_INT) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                       "No integer to indicate least significant pin index: %s\n", pin_loc_string);
    }

    int first_pin = vtr::atoi(token.data);

    token_index++;
    token = tokens[token_index];

    // Single pin is specified
    if (token.type != TOKEN_COLON) {
        if (token.type != TOKEN_CLOSE_SQUARE_BRACKET) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                           "No closing bracket: %s\n", pin_loc_string);
        }

        token_index++;

        if (token_index != num_tokens) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                           "pin location should be completed, but more tokens are present: %s\n", pin_loc_string);
        }

        freeTokens(tokens, num_tokens);
        return std::make_pair(abs_first_pin_idx + first_pin, abs_first_pin_idx + first_pin + 1);
    }

    token_index++;
    token = tokens[token_index];

    if (token.type != TOKEN_INT) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                       "No integer to indicate most significant pin index: %s\n", pin_loc_string);
    }

    int last_pin = vtr::atoi(token.data);

    token_index++;
    token = tokens[token_index];

    if (token.type != TOKEN_CLOSE_SQUARE_BRACKET) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                       "No closed square bracket: %s\n", pin_loc_string);
    }

    token_index++;

    if (token_index != num_tokens) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                       "pin location should be completed, but more tokens are present: %s\n", pin_loc_string);
    }

    if (first_pin > last_pin) {
        std::swap(first_pin, last_pin);
    }

    freeTokens(tokens, num_tokens);
    return std::make_pair(abs_first_pin_idx + first_pin, abs_first_pin_idx + last_pin + 1);
}

static void ProcessPinToPinAnnotations(pugi::xml_node Parent,
                                       t_pin_to_pin_annotation* annotation,
                                       t_pb_type* parent_pb_type,
                                       const pugiutil::loc_data& loc_data) {
    int i = 0;
    const char* Prop;

    if (get_attribute(Parent, "max", loc_data, ReqOpt::OPTIONAL).as_string(nullptr)) {
        i++;
    }
    if (get_attribute(Parent, "min", loc_data, ReqOpt::OPTIONAL).as_string(nullptr)) {
        i++;
    }
    if (get_attribute(Parent, "type", loc_data, ReqOpt::OPTIONAL).as_string(nullptr)) {
        i++;
    }
    if (get_attribute(Parent, "value", loc_data, ReqOpt::OPTIONAL).as_string(nullptr)) {
        i++;
    }
    if (0 == strcmp(Parent.name(), "C_constant")
        || 0 == strcmp(Parent.name(), "C_matrix")
        || 0 == strcmp(Parent.name(), "pack_pattern")) {
        i = 1;
    }

    annotation->num_value_prop_pairs = i;
    annotation->prop = (int*)vtr::calloc(i, sizeof(int));
    annotation->value = (char**)vtr::calloc(i, sizeof(char*));
    annotation->line_num = loc_data.line(Parent);
    /* Todo: This is slow, I should use a case lookup */
    i = 0;
    if (0 == strcmp(Parent.name(), "delay_constant")) {
        annotation->type = E_ANNOT_PIN_TO_PIN_DELAY;
        annotation->format = E_ANNOT_PIN_TO_PIN_CONSTANT;
        Prop = get_attribute(Parent, "max", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (Prop) {
            annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_DELAY_MAX;
            annotation->value[i] = vtr::strdup(Prop);
            i++;
        }
        Prop = get_attribute(Parent, "min", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (Prop) {
            annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_DELAY_MIN;
            annotation->value[i] = vtr::strdup(Prop);
            i++;
        }
        Prop = get_attribute(Parent, "in_port", loc_data).value();
        annotation->input_pins = vtr::strdup(Prop);

        Prop = get_attribute(Parent, "out_port", loc_data).value();
        annotation->output_pins = vtr::strdup(Prop);

    } else if (0 == strcmp(Parent.name(), "delay_matrix")) {
        annotation->type = E_ANNOT_PIN_TO_PIN_DELAY;
        annotation->format = E_ANNOT_PIN_TO_PIN_MATRIX;
        Prop = get_attribute(Parent, "type", loc_data).value();
        annotation->value[i] = vtr::strdup(Parent.child_value());

        if (0 == strcmp(Prop, "max")) {
            annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_DELAY_MAX;
        } else {
            VTR_ASSERT(0 == strcmp(Prop, "min"));
            annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_DELAY_MIN;
        }

        i++;
        Prop = get_attribute(Parent, "in_port", loc_data).value();
        annotation->input_pins = vtr::strdup(Prop);

        Prop = get_attribute(Parent, "out_port", loc_data).value();
        annotation->output_pins = vtr::strdup(Prop);

    } else if (0 == strcmp(Parent.name(), "C_constant")) {
        annotation->type = E_ANNOT_PIN_TO_PIN_CAPACITANCE;
        annotation->format = E_ANNOT_PIN_TO_PIN_CONSTANT;
        Prop = get_attribute(Parent, "C", loc_data).value();
        annotation->value[i] = vtr::strdup(Prop);
        annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_CAPACITANCE_C;
        i++;

        Prop = get_attribute(Parent, "in_port", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        annotation->input_pins = vtr::strdup(Prop);

        Prop = get_attribute(Parent, "out_port", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        annotation->output_pins = vtr::strdup(Prop);
        VTR_ASSERT(annotation->output_pins != nullptr || annotation->input_pins != nullptr);

    } else if (0 == strcmp(Parent.name(), "C_matrix")) {
        annotation->type = E_ANNOT_PIN_TO_PIN_CAPACITANCE;
        annotation->format = E_ANNOT_PIN_TO_PIN_MATRIX;
        annotation->value[i] = vtr::strdup(Parent.child_value());
        annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_CAPACITANCE_C;
        i++;

        Prop = get_attribute(Parent, "in_port", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        annotation->input_pins = vtr::strdup(Prop);

        Prop = get_attribute(Parent, "out_port", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        annotation->output_pins = vtr::strdup(Prop);
        VTR_ASSERT(annotation->output_pins != nullptr || annotation->input_pins != nullptr);

    } else if (0 == strcmp(Parent.name(), "T_setup")) {
        annotation->type = E_ANNOT_PIN_TO_PIN_DELAY;
        annotation->format = E_ANNOT_PIN_TO_PIN_CONSTANT;
        Prop = get_attribute(Parent, "value", loc_data).value();
        annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_DELAY_TSETUP;
        annotation->value[i] = vtr::strdup(Prop);

        i++;
        Prop = get_attribute(Parent, "port", loc_data).value();
        annotation->input_pins = vtr::strdup(Prop);

        Prop = get_attribute(Parent, "clock", loc_data).value();
        annotation->clock = vtr::strdup(Prop);

        primitives_annotation_clock_match(annotation, parent_pb_type);

    } else if (0 == strcmp(Parent.name(), "T_clock_to_Q")) {
        annotation->type = E_ANNOT_PIN_TO_PIN_DELAY;
        annotation->format = E_ANNOT_PIN_TO_PIN_CONSTANT;
        Prop = get_attribute(Parent, "max", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);

        bool found_min_max_attrib = false;
        if (Prop) {
            annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_DELAY_CLOCK_TO_Q_MAX;
            annotation->value[i] = vtr::strdup(Prop);
            i++;
            found_min_max_attrib = true;
        }
        Prop = get_attribute(Parent, "min", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (Prop) {
            annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_DELAY_CLOCK_TO_Q_MIN;
            annotation->value[i] = vtr::strdup(Prop);
            i++;
            found_min_max_attrib = true;
        }

        if (!found_min_max_attrib) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                           "Failed to find either 'max' or 'min' attribute required for <%s> in <%s>",
                           Parent.name(), Parent.parent().name());
        }

        Prop = get_attribute(Parent, "port", loc_data).value();
        annotation->input_pins = vtr::strdup(Prop);

        Prop = get_attribute(Parent, "clock", loc_data).value();
        annotation->clock = vtr::strdup(Prop);

        primitives_annotation_clock_match(annotation, parent_pb_type);

    } else if (0 == strcmp(Parent.name(), "T_hold")) {
        annotation->type = E_ANNOT_PIN_TO_PIN_DELAY;
        annotation->format = E_ANNOT_PIN_TO_PIN_CONSTANT;
        Prop = get_attribute(Parent, "value", loc_data).value();
        annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_DELAY_THOLD;
        annotation->value[i] = vtr::strdup(Prop);
        i++;

        Prop = get_attribute(Parent, "port", loc_data).value();
        annotation->input_pins = vtr::strdup(Prop);

        Prop = get_attribute(Parent, "clock", loc_data).value();
        annotation->clock = vtr::strdup(Prop);

        primitives_annotation_clock_match(annotation, parent_pb_type);

    } else if (0 == strcmp(Parent.name(), "pack_pattern")) {
        annotation->type = E_ANNOT_PIN_TO_PIN_PACK_PATTERN;
        annotation->format = E_ANNOT_PIN_TO_PIN_CONSTANT;
        Prop = get_attribute(Parent, "name", loc_data).value();
        annotation->prop[i] = (int)E_ANNOT_PIN_TO_PIN_PACK_PATTERN_NAME;
        annotation->value[i] = vtr::strdup(Prop);
        i++;

        Prop = get_attribute(Parent, "in_port", loc_data).value();
        annotation->input_pins = vtr::strdup(Prop);

        Prop = get_attribute(Parent, "out_port", loc_data).value();
        annotation->output_pins = vtr::strdup(Prop);

    } else {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                       "Unknown port type %s in %s in %s", Parent.name(),
                       Parent.parent().name(), Parent.parent().parent().name());
    }
    VTR_ASSERT(i == annotation->num_value_prop_pairs);
}

static void ProcessPb_TypePowerPinToggle(pugi::xml_node parent, t_pb_type* pb_type, const pugiutil::loc_data& loc_data) {
    pugi::xml_node cur;
    const char* prop;
    t_port* port;
    int high, low;

    cur = get_first_child(parent, "port", loc_data, ReqOpt::OPTIONAL);
    while (cur) {
        prop = get_attribute(cur, "name", loc_data).value();

        port = findPortByName(prop, pb_type, &high, &low);
        if (!port) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                           "Could not find port '%s' needed for energy per toggle.",
                           prop);
        }
        if (high != port->num_pins - 1 || low != 0) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                           "Pin-toggle does not support pin indices (%s)", prop);
        }

        if (port->port_power->pin_toggle_initialized) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                           "Duplicate pin-toggle energy for port '%s'", port->name);
        }
        port->port_power->pin_toggle_initialized = true;

        /* Get energy per toggle */
        port->port_power->energy_per_toggle = get_attribute(cur,
                                                            "energy_per_toggle", loc_data)
                                                  .as_float(0.);

        /* Get scaled by factor */
        bool reverse_scaled = false;
        prop = get_attribute(cur, "scaled_by_static_prob", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (!prop) {
            prop = get_attribute(cur, "scaled_by_static_prob_n", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
            if (prop) {
                reverse_scaled = true;
            }
        }

        if (prop) {
            port->port_power->scaled_by_port = findPortByName(prop, pb_type,
                                                              &high, &low);
            if (high != low) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                               "Pin-toggle 'scaled_by_static_prob' must be a single pin (%s)",
                               prop);
            }
            port->port_power->scaled_by_port_pin_idx = high;
            port->port_power->reverse_scaled = reverse_scaled;
        }

        cur = cur.next_sibling(cur.name());
    }
}

static void ProcessPb_TypePower(pugi::xml_node Parent, t_pb_type* pb_type, const pugiutil::loc_data& loc_data) {
    pugi::xml_node cur, child;
    bool require_dynamic_absolute = false;
    bool require_static_absolute = false;
    bool require_dynamic_C_internal = false;

    cur = get_first_child(Parent, "power", loc_data, ReqOpt::OPTIONAL);
    if (!cur) {
        return;
    }

    switch (pb_type->pb_type_power->estimation_method) {
        case POWER_METHOD_TOGGLE_PINS:
            ProcessPb_TypePowerPinToggle(cur, pb_type, loc_data);
            require_static_absolute = true;
            break;
        case POWER_METHOD_C_INTERNAL:
            require_dynamic_C_internal = true;
            require_static_absolute = true;
            break;
        case POWER_METHOD_ABSOLUTE:
            require_dynamic_absolute = true;
            require_static_absolute = true;
            break;
        default:
            break;
    }

    if (require_static_absolute) {
        child = get_single_child(cur, "static_power", loc_data);
        pb_type->pb_type_power->absolute_power_per_instance.leakage = get_attribute(child, "power_per_instance", loc_data).as_float(0.);
    }

    if (require_dynamic_absolute) {
        child = get_single_child(cur, "dynamic_power", loc_data);
        pb_type->pb_type_power->absolute_power_per_instance.dynamic = get_attribute(child, "power_per_instance", loc_data).as_float(0.);
    }

    if (require_dynamic_C_internal) {
        child = get_single_child(cur, "dynamic_power", loc_data);
        pb_type->pb_type_power->C_internal = get_attribute(child,
                                                           "C_internal", loc_data)
                                                 .as_float(0.);
    }
}

static void ProcessPb_TypePowerEstMethod(pugi::xml_node Parent, t_pb_type* pb_type, const pugiutil::loc_data& loc_data) {
    pugi::xml_node cur;
    const char* prop;

    e_power_estimation_method parent_power_method;

    prop = nullptr;

    cur = get_first_child(Parent, "power", loc_data, ReqOpt::OPTIONAL);
    if (cur) {
        prop = get_attribute(cur, "method", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
    }

    if (pb_type->parent_mode && pb_type->parent_mode->parent_pb_type) {
        parent_power_method = pb_type->parent_mode->parent_pb_type->pb_type_power->estimation_method;
    } else {
        parent_power_method = POWER_METHOD_AUTO_SIZES;
    }

    if (!prop) {
        /* default method is auto-size */
        pb_type->pb_type_power->estimation_method = power_method_inherited(parent_power_method);
    } else if (strcmp(prop, "auto-size") == 0) {
        pb_type->pb_type_power->estimation_method = POWER_METHOD_AUTO_SIZES;
    } else if (strcmp(prop, "specify-size") == 0) {
        pb_type->pb_type_power->estimation_method = POWER_METHOD_SPECIFY_SIZES;
    } else if (strcmp(prop, "pin-toggle") == 0) {
        pb_type->pb_type_power->estimation_method = POWER_METHOD_TOGGLE_PINS;
    } else if (strcmp(prop, "c-internal") == 0) {
        pb_type->pb_type_power->estimation_method = POWER_METHOD_C_INTERNAL;
    } else if (strcmp(prop, "absolute") == 0) {
        pb_type->pb_type_power->estimation_method = POWER_METHOD_ABSOLUTE;
    } else if (strcmp(prop, "ignore") == 0) {
        pb_type->pb_type_power->estimation_method = POWER_METHOD_IGNORE;
    } else if (strcmp(prop, "sum-of-children") == 0) {
        pb_type->pb_type_power->estimation_method = POWER_METHOD_SUM_OF_CHILDREN;
    } else {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                       "Invalid power estimation method for pb_type '%s'",
                       pb_type->name);
    }
}

/* Takes in a pb_type, allocates and loads data for it and recurses downwards */
static void ProcessPb_Type(pugi::xml_node Parent,
                           t_pb_type* pb_type,
                           t_mode* mode,
                           const bool timing_enabled,
                           const t_arch& arch,
                           const pugiutil::loc_data& loc_data,
                           int& pb_idx) {
    const char* Prop;
    pugi::xml_node Cur;

    bool is_root_pb_type = (mode == nullptr || mode->parent_pb_type == nullptr);
    bool is_leaf_pb_type = bool(get_attribute(Parent, "blif_model", loc_data, ReqOpt::OPTIONAL));

    std::vector<std::string> children_to_expect = {"input", "output", "clock", "mode", "power", "metadata"};
    if (!is_leaf_pb_type) {
        //Non-leafs may have a model/pb_type children
        children_to_expect.emplace_back("model");
        children_to_expect.emplace_back("pb_type");
        children_to_expect.emplace_back("interconnect");

        if (is_root_pb_type) {
            VTR_ASSERT(!is_leaf_pb_type);
            //Top level pb_type's may also have the following tag types
            children_to_expect.emplace_back("fc");
            children_to_expect.emplace_back("pinlocations");
            children_to_expect.emplace_back("switchblock_locations");
        }
    } else {
        VTR_ASSERT(is_leaf_pb_type);
        VTR_ASSERT(!is_root_pb_type);

        //Leaf pb_type's may also have the following tag types
        children_to_expect.emplace_back("T_setup");
        children_to_expect.emplace_back("T_hold");
        children_to_expect.emplace_back("T_clock_to_Q");
        children_to_expect.emplace_back("delay_constant");
        children_to_expect.emplace_back("delay_matrix");
    }

    //Sanity check contained tags
    expect_only_children(Parent, children_to_expect, loc_data);

    pb_type->parent_mode = mode;
    pb_type->index_in_logical_block = pb_idx;
    if (mode != nullptr && mode->parent_pb_type != nullptr) {
        pb_type->depth = mode->parent_pb_type->depth + 1;
        Prop = get_attribute(Parent, "name", loc_data).value();
        pb_type->name = vtr::strdup(Prop);
    } else {
        pb_type->depth = 0;
        /* same name as type */
    }

    Prop = get_attribute(Parent, "blif_model", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
    pb_type->blif_model = vtr::strdup(Prop);

    pb_type->class_type = UNKNOWN_CLASS;
    Prop = get_attribute(Parent, "class", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
    char* class_name = vtr::strdup(Prop);

    if (class_name) {
        if (0 == strcmp(class_name, PB_TYPE_CLASS_STRING[LUT_CLASS])) {
            pb_type->class_type = LUT_CLASS;
        } else if (0 == strcmp(class_name, PB_TYPE_CLASS_STRING[LATCH_CLASS])) {
            pb_type->class_type = LATCH_CLASS;
        } else if (0 == strcmp(class_name, PB_TYPE_CLASS_STRING[MEMORY_CLASS])) {
            pb_type->class_type = MEMORY_CLASS;
        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                           "Unknown class '%s' in pb_type '%s'\n", class_name,
                           pb_type->name);
        }
        free(class_name);
    }

    if (mode == nullptr) {
        pb_type->num_pb = 1;
    } else {
        pb_type->num_pb = get_attribute(Parent, "num_pb", loc_data).as_int(0);
    }

    VTR_ASSERT(pb_type->num_pb > 0);

    const int num_in_ports = count_children(Parent, "input", loc_data, ReqOpt::OPTIONAL);
    const int num_out_ports = count_children(Parent, "output", loc_data, ReqOpt::OPTIONAL);
    const int num_clock_ports = count_children(Parent, "clock", loc_data, ReqOpt::OPTIONAL);
    const int num_ports = num_in_ports + num_out_ports + num_clock_ports;
    pb_type->ports = (t_port*)vtr::calloc(num_ports, sizeof(t_port));
    pb_type->num_ports = num_ports;

    /* Enforce VPR's definition of LUT/FF by checking number of ports */
    if (pb_type->class_type == LUT_CLASS || pb_type->class_type == LATCH_CLASS) {
        if (num_in_ports != 1 || num_out_ports != 1) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                           "%s primitives must contain exactly one input port and one output port."
                           "Found '%d' input port(s) and '%d' output port(s) for '%s'",
                           (pb_type->class_type == LUT_CLASS) ? "LUT" : "Latch",
                           num_in_ports, num_out_ports, pb_type->name);
        }
    }

    /* Initialize Power Structure */
    pb_type->pb_type_power = (t_pb_type_power*)vtr::calloc(1, sizeof(t_pb_type_power));
    ProcessPb_TypePowerEstMethod(Parent, pb_type, loc_data);

    /* process ports */
    int absolute_port_first_pin_index = 0;
    int port_idx = 0;

    // STL sets for checking duplicate port names
    std::set<std::string> pb_port_names;

    for (const char* child_name : {"input", "output", "clock"}) {
        Cur = get_first_child(Parent, child_name, loc_data, ReqOpt::OPTIONAL);
        int port_index_by_type = 0;

        while (Cur) {
            pb_type->ports[port_idx].parent_pb_type = pb_type;
            pb_type->ports[port_idx].index = port_idx;
            pb_type->ports[port_idx].port_index_by_type = port_index_by_type;
            ProcessPb_TypePort(Cur, &pb_type->ports[port_idx],
                               pb_type->pb_type_power->estimation_method, is_root_pb_type, loc_data);

            pb_type->ports[port_idx].absolute_first_pin_index = absolute_port_first_pin_index;
            absolute_port_first_pin_index += pb_type->ports[port_idx].num_pins;

            //Check port name duplicates
            auto [_, success] = pb_port_names.insert(pb_type->ports[port_idx].name);
            if (!success) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                               "Duplicate port names in pb_type '%s': port '%s'\n",
                               pb_type->name, pb_type->ports[port_idx].name);
            }

            /* get next iteration */
            port_idx++;
            port_index_by_type++;
            Cur = Cur.next_sibling(Cur.name());
        }
    }

    VTR_ASSERT(port_idx == num_ports);

    /* Count stats on the number of each type of pin */
    pb_type->num_clock_pins = pb_type->num_input_pins = pb_type->num_output_pins = 0;
    for (int port_i = 0; port_i < pb_type->num_ports; port_i++) {
        if (pb_type->ports[port_i].type == IN_PORT && !pb_type->ports[port_i].is_clock) {
            pb_type->num_input_pins += pb_type->ports[port_i].num_pins;
        } else if (pb_type->ports[port_i].type == OUT_PORT) {
            pb_type->num_output_pins += pb_type->ports[port_i].num_pins;
        } else {
            VTR_ASSERT(pb_type->ports[port_i].is_clock && pb_type->ports[port_i].type == IN_PORT);
            pb_type->num_clock_pins += pb_type->ports[port_i].num_pins;
        }
    }

    pb_type->num_pins = pb_type->num_input_pins + pb_type->num_output_pins + pb_type->num_clock_pins;

    //Warn that max_internal_delay is no longer supported
    //TODO: eventually remove
    try {
        expect_child_node_count(Parent, "max_internal_delay", 0, loc_data);
    } catch (pugiutil::XmlError& e) {
        std::string msg = e.what();
        msg += ". <max_internal_delay> has been replaced with <delay_constant>/<delay_matrix> between sequential primitive ports.";
        msg += " Please upgrade your architecture file.";
        archfpga_throw(e.filename().c_str(), e.line(), msg.c_str());
    }

    pb_type->annotations = nullptr;
    pb_type->num_annotations = 0;
    /* Determine if this is a leaf or container pb_type */
    if (pb_type->blif_model != nullptr) {
        /* Process delay and capacitance annotations */
        int num_annotations = 0;
        for (auto child_name : {"delay_constant", "delay_matrix", "C_constant", "C_matrix", "T_setup", "T_clock_to_Q", "T_hold"}) {
            num_annotations += count_children(Parent, child_name, loc_data, ReqOpt::OPTIONAL);
        }

        pb_type->annotations = (t_pin_to_pin_annotation*)vtr::calloc(num_annotations, sizeof(t_pin_to_pin_annotation));
        pb_type->num_annotations = num_annotations;

        int annotation_idx = 0;
        for (auto child_name : {"delay_constant", "delay_matrix", "C_constant", "C_matrix", "T_setup", "T_clock_to_Q", "T_hold"}) {
            Cur = get_first_child(Parent, child_name, loc_data, ReqOpt::OPTIONAL);

            while (Cur) {
                ProcessPinToPinAnnotations(Cur, &pb_type->annotations[annotation_idx], pb_type, loc_data);

                /* get next iteration */
                annotation_idx++;
                Cur = Cur.next_sibling(Cur.name());
            }
        }
        VTR_ASSERT(annotation_idx == num_annotations);

        if (timing_enabled) {
            check_leaf_pb_model_timing_consistency(pb_type, arch);
        }

        /* leaf pb_type, if special known class, then read class lib otherwise treat as primitive */
        if (pb_type->class_type == LUT_CLASS) {
            ProcessLutClass(pb_type);
        } else if (pb_type->class_type == MEMORY_CLASS) {
            ProcessMemoryClass(pb_type);
        } else {
            /* other leaf pb_type do not have modes */
            pb_type->num_modes = 0;
            VTR_ASSERT(count_children(Parent, "mode", loc_data, ReqOpt::OPTIONAL) == 0);
        }
    } else {
        /* container pb_type, process modes */
        VTR_ASSERT(pb_type->class_type == UNKNOWN_CLASS);
        pb_type->num_modes = count_children(Parent, "mode", loc_data, ReqOpt::OPTIONAL);
        pb_type->pb_type_power->leakage_default_mode = 0;
        int mode_idx = 0;

        if (pb_type->num_modes == 0) {
            /* The pb_type operates in an implied one mode */
            pb_type->num_modes = 1;
            pb_type->modes = new t_mode[pb_type->num_modes];
            pb_type->modes[mode_idx].parent_pb_type = pb_type;
            pb_type->modes[mode_idx].index = mode_idx;
            ProcessMode(Parent, &pb_type->modes[mode_idx], timing_enabled, arch, loc_data, pb_idx);
            mode_idx++;
        } else {
            pb_type->modes = new t_mode[pb_type->num_modes];

            // STL set for checking duplicate mode names
            std::set<std::string> mode_names;

            Cur = get_first_child(Parent, "mode", loc_data);
            while (Cur != nullptr) {
                if (0 == strcmp(Cur.name(), "mode")) {
                    pb_type->modes[mode_idx].parent_pb_type = pb_type;
                    pb_type->modes[mode_idx].index = mode_idx;
                    ProcessMode(Cur, &pb_type->modes[mode_idx], timing_enabled, arch, loc_data, pb_idx);

                    auto [_, success] = mode_names.insert(pb_type->modes[mode_idx].name);
                    if (!success) {
                        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                                       "Duplicate mode name: '%s' in pb_type '%s'.\n",
                                       pb_type->modes[mode_idx].name, pb_type->name);
                    }

                    /* get next iteration */
                    mode_idx++;
                    Cur = Cur.next_sibling(Cur.name());
                }
            }
        }
        VTR_ASSERT(mode_idx == pb_type->num_modes);
    }

    pb_type->meta = ProcessMetadata(arch.strings, Parent, loc_data);
    ProcessPb_TypePower(Parent, pb_type, loc_data);
}

static void ProcessPb_TypePort_Power(pugi::xml_node Parent, t_port* port, e_power_estimation_method power_method, const pugiutil::loc_data& loc_data) {
    pugi::xml_node cur;
    const char* prop;
    bool wire_defined = false;

    port->port_power = (t_port_power*)vtr::calloc(1, sizeof(t_port_power));

    //Defaults
    if (power_method == POWER_METHOD_AUTO_SIZES) {
        port->port_power->wire_type = POWER_WIRE_TYPE_AUTO;
        port->port_power->buffer_type = POWER_BUFFER_TYPE_AUTO;
    } else if (power_method == POWER_METHOD_SPECIFY_SIZES) {
        port->port_power->wire_type = POWER_WIRE_TYPE_IGNORED;
        port->port_power->buffer_type = POWER_BUFFER_TYPE_NONE;
    }

    cur = get_single_child(Parent, "power", loc_data, ReqOpt::OPTIONAL);

    if (cur) {
        /* Wire capacitance */

        /* Absolute C provided */
        prop = get_attribute(cur, "wire_capacitance", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (prop) {
            if (!(power_method == POWER_METHOD_AUTO_SIZES
                  || power_method == POWER_METHOD_SPECIFY_SIZES)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                               "Wire capacitance defined for port '%s'.  This is an invalid option for the parent pb_type '%s' power estimation method.",
                               port->name, port->parent_pb_type->name);
            } else {
                wire_defined = true;
                port->port_power->wire_type = POWER_WIRE_TYPE_C;
                port->port_power->wire.C = (float)atof(prop);
            }
        }

        /* Wire absolute length provided */
        prop = get_attribute(cur, "wire_length", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (prop) {
            if (!(power_method == POWER_METHOD_AUTO_SIZES
                  || power_method == POWER_METHOD_SPECIFY_SIZES)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                               "Wire length defined for port '%s'.  This is an invalid option for the parent pb_type '%s' power estimation method.",
                               port->name, port->parent_pb_type->name);
            } else if (wire_defined) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                               "Multiple wire properties defined for port '%s', pb_type '%s'.",
                               port->name, port->parent_pb_type->name);
            } else if (strcmp(prop, "auto") == 0) {
                wire_defined = true;
                port->port_power->wire_type = POWER_WIRE_TYPE_AUTO;
            } else {
                wire_defined = true;
                port->port_power->wire_type = POWER_WIRE_TYPE_ABSOLUTE_LENGTH;
                port->port_power->wire.absolute_length = (float)atof(prop);
            }
        }

        /* Wire relative length provided */
        prop = get_attribute(cur, "wire_relative_length", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (prop) {
            if (!(power_method == POWER_METHOD_AUTO_SIZES
                  || power_method == POWER_METHOD_SPECIFY_SIZES)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                               "Wire relative length defined for port '%s'.  This is an invalid option for the parent pb_type '%s' power estimation method.",
                               port->name, port->parent_pb_type->name);
            } else if (wire_defined) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                               "Multiple wire properties defined for port '%s', pb_type '%s'.",
                               port->name, port->parent_pb_type->name);
            } else {
                wire_defined = true;
                port->port_power->wire_type = POWER_WIRE_TYPE_RELATIVE_LENGTH;
                port->port_power->wire.relative_length = (float)atof(prop);
            }
        }

        /* Buffer Size */
        prop = get_attribute(cur, "buffer_size", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (prop) {
            if (!(power_method == POWER_METHOD_AUTO_SIZES
                  || power_method == POWER_METHOD_SPECIFY_SIZES)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(cur),
                               "Buffer size defined for port '%s'.  This is an invalid option for the parent pb_type '%s' power estimation method.",
                               port->name, port->parent_pb_type->name);
            } else if (strcmp(prop, "auto") == 0) {
                port->port_power->buffer_type = POWER_BUFFER_TYPE_AUTO;
            } else {
                port->port_power->buffer_type = POWER_BUFFER_TYPE_ABSOLUTE_SIZE;
                port->port_power->buffer_size = (float)atof(prop);
            }
        }
    }
}

static void ProcessPb_TypePort(pugi::xml_node Parent, t_port* port, e_power_estimation_method power_method, const bool is_root_pb_type, const pugiutil::loc_data& loc_data) {
    std::vector<std::string> expected_attributes = {"name", "num_pins", "port_class"};
    if (is_root_pb_type) {
        expected_attributes.emplace_back("equivalent");

        if (Parent.name() == "input"s || Parent.name() == "clock"s) {
            expected_attributes.emplace_back("is_non_clock_global");
        }
    }

    expect_only_attributes(Parent, expected_attributes, loc_data);

    const char* Prop;
    Prop = get_attribute(Parent, "name", loc_data).value();
    port->name = vtr::strdup(Prop);

    Prop = get_attribute(Parent, "port_class", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
    port->port_class = vtr::strdup(Prop);

    Prop = get_attribute(Parent, "equivalent", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
    if (Prop) {
        if (Prop == "none"s) {
            port->equivalent = PortEquivalence::NONE;
        } else if (Prop == "full"s) {
            port->equivalent = PortEquivalence::FULL;
        } else if (Prop == "instance"s) {
            if (Parent.name() == "output"s) {
                port->equivalent = PortEquivalence::INSTANCE;
            } else {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Invalid pin equivalence '%s' for %s port.", Prop, Parent.name());
            }
        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                           "Invalid pin equivalence '%s'.", Prop);
        }
    }
    port->num_pins = get_attribute(Parent, "num_pins", loc_data).as_int(0);
    port->is_non_clock_global = get_attribute(Parent,
                                              "is_non_clock_global", loc_data, ReqOpt::OPTIONAL)
                                    .as_bool(false);

    if (port->num_pins <= 0) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                       "Invalid number of pins %d for %s port.", port->num_pins, Parent.name());
    }

    if (0 == strcmp(Parent.name(), "input")) {
        port->type = IN_PORT;
        port->is_clock = false;

        /* Check if LUT/FF port class is lut_in/D */
        if (port->parent_pb_type->class_type == LUT_CLASS) {
            if ((!port->port_class) || strcmp("lut_in", port->port_class)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Inputs to LUT primitives must have a port class named "
                               "as \"lut_in\".");
            }
        } else if (port->parent_pb_type->class_type == LATCH_CLASS) {
            if ((!port->port_class) || strcmp("D", port->port_class)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Input to flipflop primitives must have a port class named "
                               "as \"D\".");
            }
            /* Only allow one input pin for FF's */
            if (port->num_pins != 1) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Input port of flipflop primitives must have exactly one pin. "
                               "Found %d.",
                               port->num_pins);
            }
        }

    } else if (0 == strcmp(Parent.name(), "output")) {
        port->type = OUT_PORT;
        port->is_clock = false;

        /* Check if LUT/FF port class is lut_out/Q */
        if (port->parent_pb_type->class_type == LUT_CLASS) {
            if ((!port->port_class) || strcmp("lut_out", port->port_class)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Output to LUT primitives must have a port class named "
                               "as \"lut_in\".");
            }
            /* Only allow one output pin for LUT's */
            if (port->num_pins != 1) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Output port of LUT primitives must have exactly one pin. "
                               "Found %d.",
                               port->num_pins);
            }
        } else if (port->parent_pb_type->class_type == LATCH_CLASS) {
            if ((!port->port_class) || strcmp("Q", port->port_class)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Output to flipflop primitives must have a port class named "
                               "as \"D\".");
            }
            /* Only allow one output pin for FF's */
            if (port->num_pins != 1) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Output port of flipflop primitives must have exactly one pin. "
                               "Found %d.",
                               port->num_pins);
            }
        }
    } else if (0 == strcmp(Parent.name(), "clock")) {
        port->type = IN_PORT;
        port->is_clock = true;
        if (port->is_non_clock_global == true) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                           "Port %s cannot be both a clock and a non-clock simultaneously\n",
                           Parent.name());
        }

        if (port->parent_pb_type->class_type == LATCH_CLASS) {
            if ((!port->port_class) || strcmp("clock", port->port_class)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Clock to flipflop primitives must have a port class named "
                               "as \"clock\".");
            }
            /* Only allow one output pin for FF's */
            if (port->num_pins != 1) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Clock port of flipflop primitives must have exactly one pin. "
                               "Found %d.",
                               port->num_pins);
            }
        }
    } else {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                       "Unknown port type %s", Parent.name());
    }

    ProcessPb_TypePort_Power(Parent, port, power_method, loc_data);
}

static void ProcessInterconnect(vtr::string_internment& strings,
                                pugi::xml_node Parent,
                                t_mode* mode,
                                const pugiutil::loc_data& loc_data) {
    const char* Prop;

    // used to find duplicate names
    std::set<std::string> interconnect_names;

    int num_interconnect = 0;
    // count the total number of interconnect tags
    for (auto child_name : {"complete", "direct", "mux"}) {
        num_interconnect += count_children(Parent, child_name, loc_data, ReqOpt::OPTIONAL);
    }

    mode->num_interconnect = num_interconnect;
    mode->interconnect = new t_interconnect[num_interconnect];

    int interconnect_idx = 0;
    for (auto child_name : {"complete", "direct", "mux"}) {
        pugi::xml_node Cur = get_first_child(Parent, child_name, loc_data, ReqOpt::OPTIONAL);

        while (Cur != nullptr) {
            if (0 == strcmp(Cur.name(), "complete")) {
                mode->interconnect[interconnect_idx].type = COMPLETE_INTERC;
            } else if (0 == strcmp(Cur.name(), "direct")) {
                mode->interconnect[interconnect_idx].type = DIRECT_INTERC;
            } else {
                VTR_ASSERT(0 == strcmp(Cur.name(), "mux"));
                mode->interconnect[interconnect_idx].type = MUX_INTERC;
            }

            mode->interconnect[interconnect_idx].line_num = loc_data.line(Cur);

            mode->interconnect[interconnect_idx].parent_mode_index = mode->index;
            mode->interconnect[interconnect_idx].parent_mode = mode;

            Prop = get_attribute(Cur, "input", loc_data).value();
            mode->interconnect[interconnect_idx].input_string = vtr::strdup(Prop);

            Prop = get_attribute(Cur, "output", loc_data).value();
            mode->interconnect[interconnect_idx].output_string = vtr::strdup(Prop);

            Prop = get_attribute(Cur, "name", loc_data).value();
            mode->interconnect[interconnect_idx].name = vtr::strdup(Prop);
            mode->interconnect[interconnect_idx].meta = ProcessMetadata(strings, Cur, loc_data);

            auto [_, success] = interconnect_names.insert(mode->interconnect[interconnect_idx].name);
            if (!success) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                               "Duplicate interconnect name: '%s' in mode: '%s'.\n",
                               mode->interconnect[interconnect_idx].name, mode->name);
            }

            /* Process delay and capacitance annotations */
            int num_annotations = 0;
            for (auto annot_child_name : {"delay_constant", "delay_matrix", "C_constant", "C_matrix", "pack_pattern"}) {
                num_annotations += count_children(Cur, annot_child_name, loc_data, ReqOpt::OPTIONAL);
            }

            mode->interconnect[interconnect_idx].annotations = (t_pin_to_pin_annotation*)vtr::calloc(num_annotations,
                                                                                                     sizeof(t_pin_to_pin_annotation));
            mode->interconnect[interconnect_idx].num_annotations = num_annotations;

            int annotation_idx = 0;
            for (auto annot_child_name : {"delay_constant", "delay_matrix", "C_constant", "C_matrix", "pack_pattern"}) {
                pugi::xml_node Cur2 = get_first_child(Cur, annot_child_name, loc_data, ReqOpt::OPTIONAL);

                while (Cur2 != nullptr) {
                    ProcessPinToPinAnnotations(Cur2,
                                               &(mode->interconnect[interconnect_idx].annotations[annotation_idx]), nullptr, loc_data);

                    /* get next iteration */
                    annotation_idx++;
                    Cur2 = Cur2.next_sibling(Cur2.name());
                }
            }
            VTR_ASSERT(annotation_idx == num_annotations);

            /* Power */
            mode->interconnect[interconnect_idx].interconnect_power = (t_interconnect_power*)vtr::calloc(1,
                                                                                                         sizeof(t_interconnect_power));
            mode->interconnect[interconnect_idx].interconnect_power->port_info_initialized = false;

            /* get next iteration */
            Cur = Cur.next_sibling(Cur.name());
            interconnect_idx++;
        }
    }

    VTR_ASSERT(interconnect_idx == num_interconnect);
}

static void ProcessMode(pugi::xml_node Parent,
                        t_mode* mode,
                        const bool timing_enabled,
                        const t_arch& arch,
                        const pugiutil::loc_data& loc_data,
                        int& parent_pb_idx) {
    const char* Prop;
    pugi::xml_node Cur;

    bool implied_mode = (0 == strcmp(Parent.name(), "pb_type"));
    if (implied_mode) {
        mode->name = vtr::strdup("default");
    } else {
        Prop = get_attribute(Parent, "name", loc_data).value();
        mode->name = vtr::strdup(Prop);
    }

    /* Parse XML about if this mode is disabled for packing or not
     * By default, all the mode will be visible to packer 
     */
    mode->disable_packing = false;

    /* If the parent mode is disabled for packing,
     * all the child mode should be disabled for packing as well
     */
    if (nullptr != mode->parent_pb_type->parent_mode) {
        mode->disable_packing = mode->parent_pb_type->parent_mode->disable_packing;
    }

    /* Override if user specify */
    mode->disable_packing = get_attribute(Parent, "disable_packing", loc_data, ReqOpt::OPTIONAL).as_bool(mode->disable_packing);
    if (mode->disable_packing) {
        VTR_LOG("mode '%s[%s]' is defined by user to be disabled in packing\n",
                mode->parent_pb_type->name,
                mode->name);
    }

    mode->num_pb_type_children = count_children(Parent, "pb_type", loc_data, ReqOpt::OPTIONAL);
    if (mode->num_pb_type_children > 0) {
        mode->pb_type_children = new t_pb_type[mode->num_pb_type_children];

        // used to find duplicate pb_type names
        std::set<std::string> pb_type_names;

        int pb_type_child_idx = 0;
        Cur = get_first_child(Parent, "pb_type", loc_data);
        while (Cur != nullptr) {
            if (0 == strcmp(Cur.name(), "pb_type")) {
                parent_pb_idx++;
                ProcessPb_Type(Cur, &mode->pb_type_children[pb_type_child_idx], mode, timing_enabled, arch, loc_data, parent_pb_idx);

                auto [_, success] = pb_type_names.insert(mode->pb_type_children[pb_type_child_idx].name);
                if (!success) {
                    archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                                   "Duplicate pb_type name: '%s' in mode: '%s'.\n",
                                   mode->pb_type_children[pb_type_child_idx].name, mode->name);
                }

                /* get next iteration */
                pb_type_child_idx++;
                Cur = Cur.next_sibling(Cur.name());
            }
        }
    } else {
        mode->pb_type_children = nullptr;
    }

    /* Allocate power structure */
    mode->mode_power = (t_mode_power*)vtr::calloc(1, sizeof(t_mode_power));

    if (!implied_mode) {
        // Implied mode metadata is attached to the pb_type, rather than
        // the t_mode object.
        mode->meta = ProcessMetadata(arch.strings, Parent, loc_data);
    }

    Cur = get_single_child(Parent, "interconnect", loc_data);
    ProcessInterconnect(arch.strings, Cur, mode, loc_data);
}

static t_metadata_dict ProcessMetadata(vtr::string_internment& strings,
                                       pugi::xml_node Parent,
                                       const pugiutil::loc_data& loc_data) {
    //	<metadata>
    //	  <meta>CLBLL_L_</meta>
    //	</metadata>
    t_metadata_dict data;
    auto metadata = get_single_child(Parent, "metadata", loc_data, ReqOpt::OPTIONAL);
    if (metadata) {
        auto meta_tag = get_first_child(metadata, "meta", loc_data);
        while (meta_tag) {
            auto key = get_attribute(meta_tag, "name", loc_data).as_string();

            auto value = meta_tag.child_value();
            data.add(strings.intern_string(vtr::string_view(key)),
                     strings.intern_string(vtr::string_view(value)));
            meta_tag = meta_tag.next_sibling(meta_tag.name());
        }
    }
    return data;
}

static void Process_Fc_Values(pugi::xml_node Node, t_default_fc_spec& spec, const pugiutil::loc_data& loc_data) {
    spec.specified = true;

    /* Load the default fc_in */
    auto default_fc_in_attrib = get_attribute(Node, "in_type", loc_data);
    spec.in_value_type = string_to_fc_value_type(default_fc_in_attrib.value(), Node, loc_data);

    auto in_val_attrib = get_attribute(Node, "in_val", loc_data);
    spec.in_value = vtr::atof(in_val_attrib.value());

    /* Load the default fc_out */
    auto default_fc_out_attrib = get_attribute(Node, "out_type", loc_data);
    spec.out_value_type = string_to_fc_value_type(default_fc_out_attrib.value(), Node, loc_data);

    auto out_val_attrib = get_attribute(Node, "out_val", loc_data);
    spec.out_value = vtr::atof(out_val_attrib.value());
}

/* Takes in the node ptr for the 'fc' elements and initializes
 * the appropriate fields of type. */
static void Process_Fc(pugi::xml_node Node,
                       t_physical_tile_type* PhysicalTileType,
                       t_sub_tile* SubTile,
                       t_pin_counts pin_counts,
                       std::vector<t_segment_inf>& segments,
                       const t_default_fc_spec& arch_def_fc,
                       const pugiutil::loc_data& loc_data) {
    std::vector<t_fc_override> fc_overrides;
    t_default_fc_spec def_fc_spec;
    if (Node) {
        /* Load the default Fc values from the node */
        Process_Fc_Values(Node, def_fc_spec, loc_data);
        /* Load any <fc_override/> tags */
        for (auto child_node : Node.children()) {
            t_fc_override fc_override = Process_Fc_override(child_node, loc_data);
            fc_overrides.push_back(fc_override);
        }
    } else {
        /* Use the default value, if available */
        if (!arch_def_fc.specified) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                           "<sub_tile> is missing child <fc>, and no <default_fc> specified in architecture\n");
        }
        def_fc_spec = arch_def_fc;
    }

    /* Go through all the port/segment combinations and create the (potentially
     * overriden) pin/seg Fc specifications */
    for (size_t iseg = 0; iseg < segments.size(); ++iseg) {
        for (int icapacity = 0; icapacity < SubTile->capacity.total(); ++icapacity) {
            //If capacity > 0, we need t offset the block index by the number of pins per instance
            //this ensures that all pins have an Fc specification
            int iblk_pin = icapacity * pin_counts.total();

            for (const auto& port : SubTile->ports) {
                t_fc_specification fc_spec;

                fc_spec.seg_index = iseg;

                //Apply type and defaults
                if (port.type == IN_PORT) {
                    fc_spec.fc_type = e_fc_type::IN;
                    fc_spec.fc_value_type = def_fc_spec.in_value_type;
                    fc_spec.fc_value = def_fc_spec.in_value;
                } else {
                    VTR_ASSERT(port.type == OUT_PORT);
                    fc_spec.fc_type = e_fc_type::OUT;
                    fc_spec.fc_value_type = def_fc_spec.out_value_type;
                    fc_spec.fc_value = def_fc_spec.out_value;
                }

                //Apply any matching overrides
                bool default_overriden = false;
                for (const auto& fc_override : fc_overrides) {
                    bool apply_override = false;
                    if (!fc_override.port_name.empty() && !fc_override.seg_name.empty()) {
                        //Both port and seg names are specified require exact match on both
                        if (fc_override.port_name == port.name && fc_override.seg_name == segments[iseg].name) {
                            apply_override = true;
                        }

                    } else if (!fc_override.port_name.empty()) {
                        VTR_ASSERT(fc_override.seg_name.empty());
                        //Only the port name specified, require it to match
                        if (fc_override.port_name == port.name) {
                            apply_override = true;
                        }
                    } else {
                        VTR_ASSERT(!fc_override.seg_name.empty());
                        VTR_ASSERT(fc_override.port_name.empty());
                        //Only the seg name specified, require it to match
                        if (fc_override.seg_name == segments[iseg].name) {
                            apply_override = true;
                        }
                    }

                    if (apply_override) {
                        //Exact match, or partial match to either port or seg name
                        // Note that we continue searching, this ensures that the last matching override (in file order)
                        // is applied last

                        if (default_overriden) {
                            //Warn if multiple overrides match
                            VTR_LOGF_WARN(loc_data.filename_c_str(), loc_data.line(Node), "Multiple matching Fc overrides found; the last will be applied\n");
                        }

                        fc_spec.fc_value_type = fc_override.fc_value_type;
                        fc_spec.fc_value = fc_override.fc_value;

                        default_overriden = true;
                    }
                }

                //Add all the pins from this port
                for (int iport_pin = 0; iport_pin < port.num_pins; ++iport_pin) {
                    //XXX: this assumes that iterating through the tile ports
                    //     in order yields the block pin order
                    int true_physical_blk_pin = SubTile->sub_tile_to_tile_pin_indices[iblk_pin];
                    fc_spec.pins.push_back(true_physical_blk_pin);
                    ++iblk_pin;
                }

                PhysicalTileType->fc_specs.push_back(fc_spec);
            }
        }
    }
}

static t_fc_override Process_Fc_override(pugi::xml_node node, const pugiutil::loc_data& loc_data) {
    if (node.name() != std::string("fc_override")) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(node),
                       "Unexpeted node of type '%s' (expected optional 'fc_override')",
                       node.name());
    }

    t_fc_override fc_override;

    expect_child_node_count(node, 0, loc_data);

    bool seen_fc_type = false;
    bool seen_fc_value = false;
    bool seen_port_or_seg = false;
    for (auto attrib : node.attributes()) {
        if (attrib.name() == std::string("port_name")) {
            fc_override.port_name = attrib.value();
            seen_port_or_seg |= true;
        } else if (attrib.name() == std::string("segment_name")) {
            fc_override.seg_name = attrib.value();
            seen_port_or_seg |= true;
        } else if (attrib.name() == std::string("fc_type")) {
            fc_override.fc_value_type = string_to_fc_value_type(attrib.value(), node, loc_data);
            seen_fc_type = true;
        } else if (attrib.name() == std::string("fc_val")) {
            fc_override.fc_value = vtr::atof(attrib.value());
            seen_fc_value = true;
        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(node),
                           "Unexpected attribute '%s'", attrib.name());
        }
    }

    if (!seen_fc_type) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(node),
                       "Missing expected attribute 'fc_type'");
    }

    if (!seen_fc_value) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(node),
                       "Missing expected attribute 'fc_value'");
    }

    if (!seen_port_or_seg) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(node),
                       "Missing expected attribute(s) 'port_name' and/or 'segment_name'");
    }

    return fc_override;
}

static e_fc_value_type string_to_fc_value_type(const std::string& str, pugi::xml_node node, const pugiutil::loc_data& loc_data) {
    e_fc_value_type fc_value_type = e_fc_value_type::FRACTIONAL;

    if (str == "frac") {
        fc_value_type = e_fc_value_type::FRACTIONAL;
    } else if (str == "abs") {
        fc_value_type = e_fc_value_type::ABSOLUTE;
    } else {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(node),
                       "Invalid fc_type '%s'. Must be 'abs' or 'frac'.\n",
                       str.c_str());
    }

    return fc_value_type;
}

static void ProcessSwitchblockLocations(pugi::xml_node switchblock_locations,
                                        t_physical_tile_type* type,
                                        const t_arch& arch,
                                        const pugiutil::loc_data& loc_data) {
    VTR_ASSERT(type != nullptr);

    expect_only_attributes(switchblock_locations, {"pattern", "internal_switch"}, loc_data);

    std::string pattern = get_attribute(switchblock_locations, "pattern", loc_data, ReqOpt::OPTIONAL).as_string("external_full_internal_straight");

    //Initialize the location specs
    size_t width = type->width;
    size_t height = type->height;
    type->switchblock_locations = vtr::Matrix<e_sb_type>({{width, height}}, e_sb_type::NONE);
    type->switchblock_switch_overrides = vtr::Matrix<int>({{width, height}}, DEFAULT_SWITCH);

    if (pattern == "custom") {
        expect_only_attributes(switchblock_locations, {"pattern"}, loc_data);

        //Load a custom pattern specified with <sb_loc> tags
        expect_only_children(switchblock_locations, {"sb_loc"}, loc_data); //Only sb_loc child tags

        //Default to no SBs unless specified
        type->switchblock_locations.fill(e_sb_type::NONE);

        //Track which locations have been assigned to detect overlaps
        auto assigned_locs = vtr::Matrix<bool>({{width, height}}, false);

        for (pugi::xml_node sb_loc : switchblock_locations.children("sb_loc")) {
            expect_only_attributes(sb_loc, {"type", "xoffset", "yoffset", "switch_override"}, loc_data);

            //Determine the type
            std::string sb_type_str = get_attribute(sb_loc, "type", loc_data, ReqOpt::OPTIONAL).as_string("full");
            e_sb_type sb_type = e_sb_type::FULL;
            if (sb_type_str == "none") {
                sb_type = e_sb_type::NONE;
            } else if (sb_type_str == "horizontal") {
                sb_type = e_sb_type::HORIZONTAL;
            } else if (sb_type_str == "vertical") {
                sb_type = e_sb_type::VERTICAL;
            } else if (sb_type_str == "turns") {
                sb_type = e_sb_type::TURNS;
            } else if (sb_type_str == "straight") {
                sb_type = e_sb_type::STRAIGHT;
            } else if (sb_type_str == "full") {
                sb_type = e_sb_type::FULL;
            } else {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(sb_loc),
                               "Invalid <sb_loc> 'type' attribute '%s'\n",
                               sb_type_str.c_str());
            }

            //Determine the switch type
            int sb_switch_override = DEFAULT_SWITCH;

            auto sb_switch_override_attr = get_attribute(sb_loc, "switch_override", loc_data, ReqOpt::OPTIONAL);
            if (sb_switch_override_attr) {
                std::string sb_switch_override_str = sb_switch_override_attr.as_string();
                //Use the specified switch
                sb_switch_override = find_switch_by_name(arch.switches, sb_switch_override_str);

                if (sb_switch_override == OPEN) {
                    archfpga_throw(loc_data.filename_c_str(), loc_data.line(switchblock_locations),
                                   "Invalid <sb_loc> 'switch_override' attribute '%s' (no matching switch named '%s' found)\n",
                                   sb_switch_override_str.c_str(), sb_switch_override_str.c_str());
                }
            }

            //Get the horizontal offset
            size_t xoffset = get_attribute(sb_loc, "xoffset", loc_data, ReqOpt::OPTIONAL).as_uint(0);
            if (xoffset > width - 1) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(sb_loc),
                               "Invalid <sb_loc> 'xoffset' attribute '%zu' (must be in range [%d,%d])\n",
                               xoffset, 0, width - 1);
            }

            //Get the vertical offset
            size_t yoffset = get_attribute(sb_loc, "yoffset", loc_data, ReqOpt::OPTIONAL).as_uint(0);
            if (yoffset > height - 1) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(sb_loc),
                               "Invalid <sb_loc> 'yoffset' attribute '%zu' (must be in range [%d,%d])\n",
                               yoffset, 0, height - 1);
            }

            //Check if this location has already been set
            if (assigned_locs[xoffset][yoffset]) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(sb_loc),
                               "Duplicate <sb_loc> specifications at xoffset=%zu yoffset=%zu\n",
                               xoffset, yoffset);
            }

            //Set the custom sb location and type
            type->switchblock_locations[xoffset][yoffset] = sb_type;
            type->switchblock_switch_overrides[xoffset][yoffset] = sb_switch_override;
            assigned_locs[xoffset][yoffset] = true; //Mark the location as set for error detection
        }
    } else { //Non-custom patterns
        //Initialize defaults
        int internal_switch = DEFAULT_SWITCH;
        int external_switch = DEFAULT_SWITCH;
        e_sb_type internal_type = e_sb_type::FULL;
        e_sb_type external_type = e_sb_type::FULL;

        //Determine any internal switch override
        auto internal_switch_attr = get_attribute(switchblock_locations, "internal_switch", loc_data, ReqOpt::OPTIONAL);
        if (internal_switch_attr) {
            std::string internal_switch_name = internal_switch_attr.as_string();
            //Use the specified switch
            internal_switch = find_switch_by_name(arch.switches, internal_switch_name);

            if (internal_switch == OPEN) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(switchblock_locations),
                               "Invalid <switchblock_locations> 'internal_switch' attribute '%s' (no matching switch named '%s' found)\n",
                               internal_switch_name.c_str(), internal_switch_name.c_str());
            }
        }

        //Identify switch block types
        if (pattern == "all") {
            internal_type = e_sb_type::FULL;
            external_type = e_sb_type::FULL;

        } else if (pattern == "external") {
            internal_type = e_sb_type::NONE;
            external_type = e_sb_type::FULL;

        } else if (pattern == "internal") {
            internal_type = e_sb_type::FULL;
            external_type = e_sb_type::NONE;

        } else if (pattern == "external_full_internal_straight") {
            internal_type = e_sb_type::STRAIGHT;
            external_type = e_sb_type::FULL;

        } else if (pattern == "none") {
            internal_type = e_sb_type::NONE;
            external_type = e_sb_type::NONE;

        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(switchblock_locations),
                           "Invalid <switchblock_locations> 'pattern' attribute '%s'\n",
                           pattern.c_str());
        }

        //Fill in all locations (sets internal)
        type->switchblock_locations.fill(internal_type);
        type->switchblock_switch_overrides.fill(internal_switch);

        //Fill in top edge external
        size_t yoffset = height - 1;
        for (size_t xoffset = 0; xoffset < width; ++xoffset) {
            type->switchblock_locations[xoffset][yoffset] = external_type;
            type->switchblock_switch_overrides[xoffset][yoffset] = external_switch;
        }

        //Fill in right edge external
        size_t xoffset = width - 1;
        for (yoffset = 0; yoffset < height; ++yoffset) {
            type->switchblock_locations[xoffset][yoffset] = external_type;
            type->switchblock_switch_overrides[xoffset][yoffset] = external_switch;
        }
    }
}

/* Takes in node pointing to <models> and loads all the
 * child type objects.  */
static void ProcessModels(pugi::xml_node Node, t_arch* arch, const pugiutil::loc_data& loc_data) {
    pugi::xml_node p;
    t_model* temp = nullptr;
    int L_index;
    /* std::maps for checking duplicates */
    std::map<std::string, int> model_name_map;
    std::pair<std::map<std::string, int>::iterator, bool> ret_map_name;

    L_index = NUM_MODELS_IN_LIBRARY;

    arch->models = nullptr;
    for (pugi::xml_node model : Node.children()) {
        //Process each model
        if (model.name() != std::string("model")) {
            bad_tag(model, loc_data, Node, {"model"});
        }

        try {
            temp = new t_model;
            temp->index = L_index;
            L_index++;

            //Process the <model> tag attributes
            for (pugi::xml_attribute attr : model.attributes()) {
                if (attr.name() == std::string("never_prune")) {
                    auto model_type_str = vtr::strdup(attr.value());

                    if (std::strcmp(model_type_str, "true") == 0) {
                        temp->never_prune = true;
                    } else if (std::strcmp(model_type_str, "false") == 0) {
                        temp->never_prune = false;
                    } else {
                        archfpga_throw(loc_data.filename_c_str(), loc_data.line(model),
                                       "Unsupported never prune attribute value.");
                    }
                } else if (attr.name() == std::string("name")) {
                    if (!temp->name) {
                        //First name attr. seen
                        temp->name = vtr::strdup(attr.value());
                    } else {
                        //Duplicate name
                        archfpga_throw(loc_data.filename_c_str(), loc_data.line(model),
                                       "Duplicate 'name' attribute on <model> tag.");
                    }
                } else {
                    bad_attribute(attr, model, loc_data);
                }
            }

            /* Try insert new model, check if already exist at the same time */
            ret_map_name = model_name_map.insert(std::pair<std::string, int>(temp->name, 0));
            if (!ret_map_name.second) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(model),
                               "Duplicate model name: '%s'.\n", temp->name);
            }

            //Process the ports
            std::set<std::string> port_names;
            for (pugi::xml_node port_group : model.children()) {
                if (port_group.name() == std::string("input_ports")) {
                    ProcessModelPorts(port_group, temp, port_names, loc_data);
                } else if (port_group.name() == std::string("output_ports")) {
                    ProcessModelPorts(port_group, temp, port_names, loc_data);
                } else {
                    bad_tag(port_group, loc_data, model, {"input_ports", "output_ports"});
                }
            }

            //Sanity check the model
            check_model_clocks(temp, loc_data.filename_c_str(), loc_data.line(model));
            check_model_combinational_sinks(temp, loc_data.filename_c_str(), loc_data.line(model));
            warn_model_missing_timing(temp, loc_data.filename_c_str(), loc_data.line(model));
        } catch (ArchFpgaError& e) {
            free_arch_model(temp);
            throw;
        }

        //Add the model
        temp->next = arch->models;
        arch->models = temp;
    }
    return;
}

static void ProcessModelPorts(pugi::xml_node port_group, t_model* model, std::set<std::string>& port_names, const pugiutil::loc_data& loc_data) {
    for (pugi::xml_attribute attr : port_group.attributes()) {
        bad_attribute(attr, port_group, loc_data);
    }

    enum PORTS dir = ERR_PORT;
    if (port_group.name() == std::string("input_ports")) {
        dir = IN_PORT;
    } else {
        VTR_ASSERT(port_group.name() == std::string("output_ports"));
        dir = OUT_PORT;
    }

    //Process each port
    for (pugi::xml_node port : port_group.children()) {
        //Should only be ports
        if (port.name() != std::string("port")) {
            bad_tag(port, loc_data, port_group, {"port"});
        }

        //Ports should have no children
        for (pugi::xml_node port_child : port.children()) {
            bad_tag(port_child, loc_data, port);
        }

        t_model_ports* model_port = new t_model_ports;

        model_port->dir = dir;

        //Process the attributes of each port
        for (pugi::xml_attribute attr : port.attributes()) {
            if (attr.name() == std::string("name")) {
                model_port->name = vtr::strdup(attr.value());

            } else if (attr.name() == std::string("is_clock")) {
                model_port->is_clock = attribute_to_bool(port, attr, loc_data);

            } else if (attr.name() == std::string("is_non_clock_global")) {
                model_port->is_non_clock_global = attribute_to_bool(port, attr, loc_data);

            } else if (attr.name() == std::string("clock")) {
                model_port->clock = std::string(attr.value());

            } else if (attr.name() == std::string("combinational_sink_ports")) {
                model_port->combinational_sink_ports = vtr::split(attr.value());

            } else {
                bad_attribute(attr, port, loc_data);
            }
        }

        //Sanity checks
        if (model_port->is_clock == true && model_port->is_non_clock_global == true) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(port),
                           "Model port '%s' cannot be both a clock and a non-clock signal simultaneously", model_port->name);
        }

        if (model_port->name == nullptr) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(port),
                           "Model port is missing a name");
        }

        if (port_names.count(model_port->name)) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(port),
                           "Duplicate model port named '%s'", model_port->name);
        }

        if (dir == OUT_PORT && !model_port->combinational_sink_ports.empty()) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(port),
                           "Model output ports can not have combinational sink ports");
        }

        //Add the port
        if (dir == IN_PORT) {
            model_port->next = model->inputs;
            model->inputs = model_port;

        } else {
            VTR_ASSERT(dir == OUT_PORT);

            model_port->next = model->outputs;
            model->outputs = model_port;
        }
    }
}

static void ProcessLayout(pugi::xml_node layout_tag, t_arch* arch, const pugiutil::loc_data& loc_data, int& num_of_avail_layer) {
    VTR_ASSERT(layout_tag.name() == std::string("layout"));

    //Expect no attributes on <layout>
    expect_only_attributes(layout_tag, {}, loc_data);

    //Count the number of <auto_layout> or <fixed_layout> tags
    size_t auto_layout_cnt = 0;
    size_t fixed_layout_cnt = 0;
    for (auto layout_type_tag : layout_tag.children()) {
        if (layout_type_tag.name() == std::string("auto_layout")) {
            ++auto_layout_cnt;
        } else if (layout_type_tag.name() == std::string("fixed_layout")) {
            ++fixed_layout_cnt;
        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(layout_type_tag),
                           "Unexpected tag type '<%s>', expected '<auto_layout>' or '<fixed_layout>'", layout_type_tag.name());
        }
    }

    if (auto_layout_cnt == 0 && fixed_layout_cnt == 0) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(layout_tag),
                       "Expected either an <auto_layout> or <fixed_layout> tag");
    }
    if (auto_layout_cnt > 1) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(layout_tag),
                       "Expected at most one <auto_layout> tag");
    }
    VTR_ASSERT_MSG(auto_layout_cnt == 0 || auto_layout_cnt == 1, "<auto_layout> may appear at most once");

    for (auto layout_type_tag : layout_tag.children()) {
        t_grid_def grid_def = ProcessGridLayout(arch->strings, layout_type_tag, loc_data, arch, num_of_avail_layer);

        arch->grid_layouts.emplace_back(std::move(grid_def));
    }
}

static t_grid_def ProcessGridLayout(vtr::string_internment& strings,
                                    pugi::xml_node layout_type_tag,
                                    const pugiutil::loc_data& loc_data,
                                    t_arch* arch,
                                    int& num_of_avail_layer) {
    t_grid_def grid_def;
    num_of_avail_layer = get_number_of_layers(layout_type_tag, loc_data);
    bool has_layer = layout_type_tag.child("layer");

    //Determine the grid specification type
    if (layout_type_tag.name() == std::string("auto_layout")) {
        expect_only_attributes(layout_type_tag, {"aspect_ratio"}, loc_data);

        grid_def.grid_type = GridDefType::AUTO;

        grid_def.aspect_ratio = get_attribute(layout_type_tag, "aspect_ratio", loc_data, ReqOpt::OPTIONAL).as_float(1.);
        grid_def.name = "auto";

    } else if (layout_type_tag.name() == std::string("fixed_layout")) {
        expect_only_attributes(layout_type_tag, {"width", "height", "name"}, loc_data);

        grid_def.grid_type = GridDefType::FIXED;
        grid_def.width = get_attribute(layout_type_tag, "width", loc_data).as_int();
        grid_def.height = get_attribute(layout_type_tag, "height", loc_data).as_int();
        std::string name = get_attribute(layout_type_tag, "name", loc_data).value();

        if (name == "auto") {
            //We name <auto_layout> as 'auto', so don't allow a user to specify it
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(layout_type_tag),
                           "The name '%s' is reserved for auto-sized layouts; please choose another name");
        }
        grid_def.name = name;

    } else {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(layout_type_tag),
                       "Unexpected tag '<%s>'. Expected '<auto_layout>' or '<fixed_layout>'.",
                       layout_type_tag.name());
    }

    grid_def.layers.resize(num_of_avail_layer);
    arch->layer_global_routing.resize(num_of_avail_layer);
    //No layer tag is specified (only one die is specified in the arch file)
    //Need to process layout_type_tag children to get block types locations in the grid
    if (has_layer) {
        std::set<int> seen_die_numbers; //Check that die numbers in the specific layout tag are unique
        //One or more than one layer tag is specified
        auto layer_tag_specified = layout_type_tag.children("layer");
        for (auto layer_child : layer_tag_specified) {
            int die_number;
            bool has_global_routing;
            //More than one layer tag is specified, meaning that multi-die FPGA is specified in the arch file
            //Need to process each <layer> tag children to get block types locations for each grid
            die_number = get_attribute(layer_child, "die", loc_data).as_int(0);
            has_global_routing = get_attribute(layer_child, "has_prog_routing", loc_data, ReqOpt::OPTIONAL).as_bool(true);
            arch->layer_global_routing.at(die_number) = has_global_routing;
            VTR_ASSERT(die_number >= 0 && die_number < num_of_avail_layer);
            auto insert_res = seen_die_numbers.insert(die_number);
            VTR_ASSERT_MSG(insert_res.second, "Two different layers with a same die number may have been specified in the Architecture file");
            ProcessBlockTypeLocs(grid_def, die_number, strings, layer_child, loc_data);
        }
    } else {
        //if only one die is available, then global routing resources must exist in that die
        int die_number = 0;
        arch->layer_global_routing.at(die_number) = true;
        ProcessBlockTypeLocs(grid_def, die_number, strings, layout_type_tag, loc_data);
    }
    return grid_def;
}

static void ProcessBlockTypeLocs(t_grid_def& grid_def,
                                 int die_number,
                                 vtr::string_internment& strings,
                                 pugi::xml_node layout_block_type_tag,
                                 const pugiutil::loc_data& loc_data) {
    //Process all the block location specifications
    for (auto loc_spec_tag : layout_block_type_tag.children()) {
        auto loc_type = loc_spec_tag.name();
        auto type_name = get_attribute(loc_spec_tag, "type", loc_data).value();
        int priority = get_attribute(loc_spec_tag, "priority", loc_data).as_int();
        t_metadata_dict meta = ProcessMetadata(strings, loc_spec_tag, loc_data);

        if (loc_type == std::string("perimeter")) {
            expect_only_attributes(loc_spec_tag, {"type", "priority"}, loc_data);

            //The edges
            t_grid_loc_def left_edge(type_name, priority); //Including corners
            left_edge.x.start_expr = "0";
            left_edge.x.end_expr = "0";
            left_edge.y.start_expr = "0";
            left_edge.y.end_expr = "H - 1";

            t_grid_loc_def right_edge(type_name, priority); //Including corners
            right_edge.x.start_expr = "W - 1";
            right_edge.x.end_expr = "W - 1";
            right_edge.y.start_expr = "0";
            right_edge.y.end_expr = "H - 1";

            t_grid_loc_def bottom_edge(type_name, priority); //Excluding corners
            bottom_edge.x.start_expr = "1";
            bottom_edge.x.end_expr = "W - 2";
            bottom_edge.y.start_expr = "0";
            bottom_edge.y.end_expr = "0";

            t_grid_loc_def top_edge(type_name, priority); //Excluding corners
            top_edge.x.start_expr = "1";
            top_edge.x.end_expr = "W - 2";
            top_edge.y.start_expr = "H - 1";
            top_edge.y.end_expr = "H - 1";

            left_edge.owned_meta = std::make_unique<t_metadata_dict>(meta);
            left_edge.meta = left_edge.owned_meta.get();
            right_edge.meta = left_edge.owned_meta.get();
            top_edge.meta = left_edge.owned_meta.get();
            bottom_edge.meta = left_edge.owned_meta.get();

            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(left_edge));
            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(right_edge));
            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(top_edge));
            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(bottom_edge));

        } else if (loc_type == std::string("corners")) {
            expect_only_attributes(loc_spec_tag, {"type", "priority"}, loc_data);

            //The corners
            t_grid_loc_def bottom_left(type_name, priority);
            bottom_left.x.start_expr = "0";
            bottom_left.x.end_expr = "0";
            bottom_left.y.start_expr = "0";
            bottom_left.y.end_expr = "0";

            t_grid_loc_def top_left(type_name, priority);
            top_left.x.start_expr = "0";
            top_left.x.end_expr = "0";
            top_left.y.start_expr = "H-1";
            top_left.y.end_expr = "H-1";

            t_grid_loc_def bottom_right(type_name, priority);
            bottom_right.x.start_expr = "W-1";
            bottom_right.x.end_expr = "W-1";
            bottom_right.y.start_expr = "0";
            bottom_right.y.end_expr = "0";

            t_grid_loc_def top_right(type_name, priority);
            top_right.x.start_expr = "W-1";
            top_right.x.end_expr = "W-1";
            top_right.y.start_expr = "H-1";
            top_right.y.end_expr = "H-1";

            bottom_left.owned_meta = std::make_unique<t_metadata_dict>(meta);
            bottom_left.meta = bottom_left.owned_meta.get();
            top_left.meta = bottom_left.owned_meta.get();
            bottom_right.meta = bottom_left.owned_meta.get();
            top_right.meta = bottom_left.owned_meta.get();

            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(bottom_left));
            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(top_left));
            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(bottom_right));
            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(top_right));

        } else if (loc_type == std::string("fill")) {
            expect_only_attributes(loc_spec_tag, {"type", "priority"}, loc_data);

            t_grid_loc_def fill(type_name, priority);
            fill.x.start_expr = "0";
            fill.x.end_expr = "W - 1";
            fill.y.start_expr = "0";
            fill.y.end_expr = "H - 1";

            fill.owned_meta = std::make_unique<t_metadata_dict>(meta);
            fill.meta = fill.owned_meta.get();

            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(fill));

        } else if (loc_type == std::string("single")) {
            expect_only_attributes(loc_spec_tag, {"type", "priority", "x", "y"}, loc_data);

            t_grid_loc_def single(type_name, priority);
            single.x.start_expr = get_attribute(loc_spec_tag, "x", loc_data).value();
            single.y.start_expr = get_attribute(loc_spec_tag, "y", loc_data).value();
            single.x.end_expr = single.x.start_expr + " + w - 1";
            single.y.end_expr = single.y.start_expr + " + h - 1";

            single.owned_meta = std::make_unique<t_metadata_dict>(meta);
            single.meta = single.owned_meta.get();

            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(single));

        } else if (loc_type == std::string("col")) {
            expect_only_attributes(loc_spec_tag, {"type", "priority", "startx", "repeatx", "starty", "incry"}, loc_data);

            t_grid_loc_def col(type_name, priority);

            auto startx_attr = get_attribute(loc_spec_tag, "startx", loc_data);

            col.x.start_expr = startx_attr.value();
            col.x.end_expr = startx_attr.value() + std::string(" + w - 1"); //end is inclusive so need to include block width

            auto repeat_attr = get_attribute(loc_spec_tag, "repeatx", loc_data, ReqOpt::OPTIONAL);
            if (repeat_attr) {
                col.x.repeat_expr = repeat_attr.value();
            }

            auto starty_attr = get_attribute(loc_spec_tag, "starty", loc_data, ReqOpt::OPTIONAL);
            if (starty_attr) {
                col.y.start_expr = starty_attr.value();
            }

            auto incry_attr = get_attribute(loc_spec_tag, "incry", loc_data, ReqOpt::OPTIONAL);
            if (incry_attr) {
                col.y.incr_expr = incry_attr.value();
            }

            col.owned_meta = std::make_unique<t_metadata_dict>(meta);
            col.meta = col.owned_meta.get();

            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(col));

        } else if (loc_type == std::string("row")) {
            expect_only_attributes(loc_spec_tag, {"type", "priority", "starty", "repeaty", "startx", "incrx"}, loc_data);

            t_grid_loc_def row(type_name, priority);

            auto starty_attr = get_attribute(loc_spec_tag, "starty", loc_data);

            row.y.start_expr = starty_attr.value();
            row.y.end_expr = starty_attr.value() + std::string(" + h - 1"); //end is inclusive so need to include block height

            auto repeat_attr = get_attribute(loc_spec_tag, "repeaty", loc_data, ReqOpt::OPTIONAL);
            if (repeat_attr) {
                row.y.repeat_expr = repeat_attr.value();
            }

            auto startx_attr = get_attribute(loc_spec_tag, "startx", loc_data, ReqOpt::OPTIONAL);
            if (startx_attr) {
                row.x.start_expr = startx_attr.value();
            }

            auto incrx_attr = get_attribute(loc_spec_tag, "incrx", loc_data, ReqOpt::OPTIONAL);
            if (incrx_attr) {
                row.x.incr_expr = incrx_attr.value();
            }

            row.owned_meta = std::make_unique<t_metadata_dict>(meta);
            row.meta = row.owned_meta.get();

            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(row));
        } else if (loc_type == std::string("region")) {
            expect_only_attributes(loc_spec_tag,
                                   {"type", "priority",
                                    "startx", "endx", "repeatx", "incrx",
                                    "starty", "endy", "repeaty", "incry"},
                                   loc_data);
            t_grid_loc_def region(type_name, priority);

            auto startx_attr = get_attribute(loc_spec_tag, "startx", loc_data, ReqOpt::OPTIONAL);
            if (startx_attr) {
                region.x.start_expr = startx_attr.value();
            }

            auto endx_attr = get_attribute(loc_spec_tag, "endx", loc_data, ReqOpt::OPTIONAL);
            if (endx_attr) {
                region.x.end_expr = endx_attr.value();
            }

            auto starty_attr = get_attribute(loc_spec_tag, "starty", loc_data, ReqOpt::OPTIONAL);
            if (starty_attr) {
                region.y.start_expr = starty_attr.value();
            }

            auto endy_attr = get_attribute(loc_spec_tag, "endy", loc_data, ReqOpt::OPTIONAL);
            if (endy_attr) {
                region.y.end_expr = endy_attr.value();
            }

            auto repeatx_attr = get_attribute(loc_spec_tag, "repeatx", loc_data, ReqOpt::OPTIONAL);
            if (repeatx_attr) {
                region.x.repeat_expr = repeatx_attr.value();
            }

            auto repeaty_attr = get_attribute(loc_spec_tag, "repeaty", loc_data, ReqOpt::OPTIONAL);
            if (repeaty_attr) {
                region.y.repeat_expr = repeaty_attr.value();
            }

            auto incrx_attr = get_attribute(loc_spec_tag, "incrx", loc_data, ReqOpt::OPTIONAL);
            if (incrx_attr) {
                region.x.incr_expr = incrx_attr.value();
            }

            auto incry_attr = get_attribute(loc_spec_tag, "incry", loc_data, ReqOpt::OPTIONAL);
            if (incry_attr) {
                region.y.incr_expr = incry_attr.value();
            }

            region.owned_meta = std::make_unique<t_metadata_dict>(meta);
            region.meta = region.owned_meta.get();

            grid_def.layers.at(die_number).loc_defs.emplace_back(std::move(region));
        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(loc_spec_tag),
                           "Unrecognized grid location specification type '%s'\n", loc_type);
        }
    }
}

static int get_number_of_layers(pugi::xml_node layout_type_tag, const pugiutil::loc_data& loc_data) {
    int max_die_num = -1;

    const auto& layer_tag = layout_type_tag.children("layer");
    for (const auto& layer_child : layer_tag) {
        int die_number = get_attribute(layer_child, "die", loc_data).as_int(0);
        if (die_number > max_die_num) {
            max_die_num = die_number;
        }
    }

    if (max_die_num == -1) {
        // For backwards compatibility, if no die number is specified, assume 1 layer
        return 1;
    } else {
        return max_die_num + 1;
    }
}

/* Takes in node pointing to <device> and loads all the
 * child type objects. */
static void ProcessDevice(pugi::xml_node Node, t_arch* arch, t_default_fc_spec& arch_def_fc, const pugiutil::loc_data& loc_data) {
    const char* Prop;
    pugi::xml_node Cur;
    bool custom_switch_block = false;

    //Warn that <timing> is no longer supported
    //TODO: eventually remove
    try {
        expect_child_node_count(Node, "timing", 0, loc_data);
    } catch (pugiutil::XmlError& e) {
        std::string msg = e.what();
        msg += ". <timing> has been replaced with the <switch_block> tag.";
        msg += " Please upgrade your architecture file.";
        archfpga_throw(e.filename().c_str(), e.line(), msg.c_str());
    }

    expect_only_children(Node, {"sizing", "area", "chan_width_distr", "switch_block", "connection_block", "default_fc"}, loc_data);

    //<sizing> tag
    Cur = get_single_child(Node, "sizing", loc_data);
    expect_only_attributes(Cur, {"R_minW_nmos", "R_minW_pmos"}, loc_data);
    arch->R_minW_nmos = get_attribute(Cur, "R_minW_nmos", loc_data).as_float();
    arch->R_minW_pmos = get_attribute(Cur, "R_minW_pmos", loc_data).as_float();

    //<area> tag
    Cur = get_single_child(Node, "area", loc_data);
    expect_only_attributes(Cur, {"grid_logic_tile_area"}, loc_data);
    arch->grid_logic_tile_area = get_attribute(Cur, "grid_logic_tile_area",
                                               loc_data, ReqOpt::OPTIONAL)
                                     .as_float(0);

    //<chan_width_distr> tag
    Cur = get_single_child(Node, "chan_width_distr", loc_data, ReqOpt::OPTIONAL);
    expect_only_attributes(Cur, {}, loc_data);
    if (Cur != nullptr) {
        ProcessChanWidthDistr(Cur, arch, loc_data);
    }

    //<connection_block> tag
    Cur = get_single_child(Node, "connection_block", loc_data);
    expect_only_attributes(Cur, {"input_switch_name", "input_inter_die_switch_name"}, loc_data);
    arch->ipin_cblock_switch_name.emplace_back(get_attribute(Cur, "input_switch_name", loc_data).as_string());
    std::string inter_die_conn = get_attribute(Cur, "input_inter_die_switch_name", loc_data, ReqOpt::OPTIONAL).as_string("");
    if (inter_die_conn != "") {
        arch->ipin_cblock_switch_name.push_back(inter_die_conn);
    }

    //<switch_block> tag
    Cur = get_single_child(Node, "switch_block", loc_data);
    expect_only_attributes(Cur, {"type", "fs"}, loc_data);
    Prop = get_attribute(Cur, "type", loc_data).value();
    if (strcmp(Prop, "wilton") == 0) {
        arch->SBType = WILTON;
    } else if (strcmp(Prop, "universal") == 0) {
        arch->SBType = UNIVERSAL;
    } else if (strcmp(Prop, "subset") == 0) {
        arch->SBType = SUBSET;
    } else if (strcmp(Prop, "custom") == 0) {
        arch->SBType = CUSTOM;
        custom_switch_block = true;
    } else {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                       "Unknown property %s for switch block type x\n", Prop);
    }

    ReqOpt CUSTOM_SWITCHBLOCK_REQD = BoolToReqOpt(!custom_switch_block);
    arch->Fs = get_attribute(Cur, "fs", loc_data, CUSTOM_SWITCHBLOCK_REQD).as_int(3);

    Cur = get_single_child(Node, "default_fc", loc_data, ReqOpt::OPTIONAL);
    if (Cur) {
        arch_def_fc.specified = true;
        expect_only_attributes(Cur, {"in_type", "in_val", "out_type", "out_val"}, loc_data);
        Process_Fc_Values(Cur, arch_def_fc, loc_data);
    } else {
        arch_def_fc.specified = false;
    }
}

/* Takes in node pointing to <chan_width_distr> and loads all the
 * child type objects. */
static void ProcessChanWidthDistr(pugi::xml_node Node,
                                  t_arch* arch,
                                  const pugiutil::loc_data& loc_data) {
    pugi::xml_node Cur;

    expect_only_children(Node, {"x", "y"}, loc_data);

    Cur = get_single_child(Node, "x", loc_data);
    ProcessChanWidthDistrDir(Cur, &arch->Chans.chan_x_dist, loc_data);

    Cur = get_single_child(Node, "y", loc_data);
    ProcessChanWidthDistrDir(Cur, &arch->Chans.chan_y_dist, loc_data);
}

/* Takes in node within <chan_width_distr> and loads all the
 * child type objects. */
static void ProcessChanWidthDistrDir(pugi::xml_node Node, t_chan* chan, const pugiutil::loc_data& loc_data) {
    const char* Prop;

    ReqOpt hasXpeak, hasWidth, hasDc;
    hasXpeak = hasWidth = hasDc = ReqOpt::OPTIONAL;

    Prop = get_attribute(Node, "distr", loc_data).value();
    if (strcmp(Prop, "uniform") == 0) {
        chan->type = UNIFORM;
    } else if (strcmp(Prop, "gaussian") == 0) {
        chan->type = GAUSSIAN;
        hasXpeak = hasWidth = hasDc = ReqOpt::REQUIRED;
    } else if (strcmp(Prop, "pulse") == 0) {
        chan->type = PULSE;
        hasXpeak = hasWidth = hasDc = ReqOpt::REQUIRED;
    } else if (strcmp(Prop, "delta") == 0) {
        hasXpeak = hasDc = ReqOpt::REQUIRED;
        chan->type = DELTA;
    } else {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                       "Unknown property %s for chan_width_distr x\n", Prop);
    }

    chan->peak = get_attribute(Node, "peak", loc_data).as_float(UNDEFINED);
    chan->width = get_attribute(Node, "width", loc_data, hasWidth).as_float(0);
    chan->xpeak = get_attribute(Node, "xpeak", loc_data, hasXpeak).as_float(0);
    chan->dc = get_attribute(Node, "dc", loc_data, hasDc).as_float(0);
}

static void ProcessTiles(pugi::xml_node Node,
                         std::vector<t_physical_tile_type>& PhysicalTileTypes,
                         std::vector<t_logical_block_type>& LogicalBlockTypes,
                         const t_default_fc_spec& arch_def_fc,
                         t_arch& arch,
                         const pugiutil::loc_data& loc_data,
                         const int num_of_avail_layer) {

    // used to find duplicate tile names
    std::set<std::string> tile_type_descriptors;

    /* Alloc the type list. Need one additional t_type_descriptors:
     * 1: empty pseudo-type
     */
    t_physical_tile_type EMPTY_PHYSICAL_TILE_TYPE = get_empty_physical_type();
    EMPTY_PHYSICAL_TILE_TYPE.index = 0;
    PhysicalTileTypes.push_back(EMPTY_PHYSICAL_TILE_TYPE);

    /* Process the types */
    int index = 1; /* Skip over 'empty' type */

    pugi::xml_node CurTileType = Node.first_child();
    while (CurTileType) {
        check_node(CurTileType, "tile", loc_data);

        t_physical_tile_type PhysicalTileType;

        PhysicalTileType.index = index;

        /* Parses the properties fields of the type */
        ProcessTileProps(CurTileType, &PhysicalTileType, loc_data);

        auto [_, success] = tile_type_descriptors.insert(PhysicalTileType.name);
        if (!success) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(CurTileType),
                           "Duplicate tile descriptor name: '%s'.\n", PhysicalTileType.name.c_str());
        }

        //Warn that gridlocations is no longer supported
        //TODO: eventually remove
        try {
            expect_child_node_count(CurTileType, "gridlocations", 0, loc_data);
        } catch (pugiutil::XmlError& e) {
            std::string msg = e.what();
            msg += ". <gridlocations> has been replaced by the <auto_layout> and <device_layout> tags in the <layout> section.";
            msg += " Please upgrade your architecture file.";
            archfpga_throw(e.filename().c_str(), e.line(), msg.c_str());
        }

        //Load switchblock type and location overrides
        pugi::xml_node Cur = get_single_child(CurTileType, "switchblock_locations", loc_data, ReqOpt::OPTIONAL);
        ProcessSwitchblockLocations(Cur, &PhysicalTileType, arch, loc_data);

        ProcessSubTiles(CurTileType, &PhysicalTileType, LogicalBlockTypes, arch.Segments, arch_def_fc, loc_data, num_of_avail_layer);

        /* Type fully read */
        ++index;

        /* Push newly created Types to corresponding vectors */
        PhysicalTileTypes.push_back(PhysicalTileType);

        /* Free this node and get its next sibling node */
        CurTileType = CurTileType.next_sibling(CurTileType.name());
    }
}

static void MarkIoTypes(std::vector<t_physical_tile_type>& PhysicalTileTypes) {
    for (auto& type : PhysicalTileTypes) {
        type.is_input_type = false;
        type.is_output_type = false;

        auto equivalent_sites = get_equivalent_sites_set(&type);

        for (const auto& equivalent_site : equivalent_sites) {
            if (block_type_contains_blif_model(equivalent_site, MODEL_INPUT)) {
                type.is_input_type = true;
                break;
            }
        }

        for (const auto& equivalent_site : equivalent_sites) {
            if (block_type_contains_blif_model(equivalent_site, MODEL_OUTPUT)) {
                type.is_output_type = true;
                break;
            }
        }
    }
}

static void ProcessTileProps(pugi::xml_node Node,
                             t_physical_tile_type* PhysicalTileType,
                             const pugiutil::loc_data& loc_data) {
    expect_only_attributes(Node, {"name", "width", "height", "area"}, loc_data);

    /* Load type name */
    auto Prop = get_attribute(Node, "name", loc_data).value();
    PhysicalTileType->name = Prop;

    /* Load properties */
    PhysicalTileType->width = get_attribute(Node, "width", loc_data, ReqOpt::OPTIONAL).as_uint(1);
    PhysicalTileType->height = get_attribute(Node, "height", loc_data, ReqOpt::OPTIONAL).as_uint(1);
    PhysicalTileType->area = get_attribute(Node, "area", loc_data, ReqOpt::OPTIONAL).as_float(UNDEFINED);

    if (atof(Prop) < 0) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                       "Area for type %s must be non-negative\n", PhysicalTileType->name.c_str());
    }
}

static t_pin_counts ProcessSubTilePorts(pugi::xml_node Parent,
                                        t_sub_tile* SubTile,
                                        const pugiutil::loc_data& loc_data) {
    pugi::xml_node Cur;

    int num_ports = 0;
    for (auto port_type : {"input", "output", "clock"}) {
        num_ports += count_children(Parent, port_type, loc_data, ReqOpt::OPTIONAL);
    }

    int port_index = 0;
    int absolute_first_pin_index = 0;

    // used to find duplicate port names
    std::set<std::string> sub_tile_port_names;

    for (auto port_type : {"input", "output", "clock"}) {
        int port_index_by_type = 0;
        Cur = get_first_child(Parent, port_type, loc_data, ReqOpt::OPTIONAL);
        while (Cur) {
            t_physical_tile_port port;

            port.index = port_index;
            port.absolute_first_pin_index = absolute_first_pin_index;
            port.port_index_by_type = port_index_by_type;
            ProcessTilePort(Cur, &port, loc_data);

            //Check port name duplicates
            auto [_, subtile_success] = sub_tile_port_names.insert(port.name);
            if (!subtile_success) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                               "Duplicate port names in subtile '%s': port '%s'\n",
                               SubTile->name.c_str(), port.name);
            }

            //Push port
            SubTile->ports.push_back(port);

            /* get next iteration */
            port_index++;
            port_index_by_type++;
            absolute_first_pin_index += port.num_pins;

            Cur = Cur.next_sibling(Cur.name());
        }
    }

    VTR_ASSERT(port_index == num_ports);

    t_pin_counts pin_counts;

    /* Count stats on the number of each type of pin */
    for (const auto& port : SubTile->ports) {
        if (port.type == IN_PORT && !port.is_clock) {
            pin_counts.input += port.num_pins;
        } else if (port.type == OUT_PORT) {
            pin_counts.output += port.num_pins;
        } else {
            VTR_ASSERT(port.is_clock && port.type == IN_PORT);
            pin_counts.clock += port.num_pins;
        }
    }

    return pin_counts;
}

static void ProcessTilePort(pugi::xml_node Node,
                            t_physical_tile_port* port,
                            const pugiutil::loc_data& loc_data) {
    std::vector<std::string> expected_attributes = {"name", "num_pins", "equivalent"};

    if (Node.name() == "input"s || Node.name() == "clock"s) {
        expected_attributes.emplace_back("is_non_clock_global");
    }

    expect_only_attributes(Node, expected_attributes, loc_data);

    const char* Prop;
    Prop = get_attribute(Node, "name", loc_data).value();
    port->name = vtr::strdup(Prop);

    Prop = get_attribute(Node, "equivalent", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
    if (Prop) {
        if (Prop == "none"s) {
            port->equivalent = PortEquivalence::NONE;
        } else if (Prop == "full"s) {
            port->equivalent = PortEquivalence::FULL;
        } else if (Prop == "instance"s) {
            if (Node.name() == "output"s) {
                port->equivalent = PortEquivalence::INSTANCE;
            } else {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                               "Invalid pin equivalence '%s' for %s port.", Prop, Node.name());
            }
        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                           "Invalid pin equivalence '%s'.", Prop);
        }
    }
    port->num_pins = get_attribute(Node, "num_pins", loc_data).as_int(0);
    port->is_non_clock_global = get_attribute(Node,
                                              "is_non_clock_global", loc_data, ReqOpt::OPTIONAL)
                                    .as_bool(false);

    if (port->num_pins <= 0) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                       "Invalid number of pins %d for %s port.", port->num_pins, Node.name());
    }

    if (0 == strcmp(Node.name(), "input")) {
        port->type = IN_PORT;
        port->is_clock = false;

    } else if (0 == strcmp(Node.name(), "output")) {
        port->type = OUT_PORT;
        port->is_clock = false;

    } else if (0 == strcmp(Node.name(), "clock")) {
        port->type = IN_PORT;
        port->is_clock = true;

        if (port->is_non_clock_global) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                           "Port %s cannot be both a clock and a non-clock simultaneously\n",
                           Node.name());
        }

    } else {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                       "Unknown port type %s", Node.name());
    }
}

static void ProcessTileEquivalentSites(pugi::xml_node Parent,
                                       t_sub_tile* SubTile,
                                       t_physical_tile_type* PhysicalTileType,
                                       std::vector<t_logical_block_type>& LogicalBlockTypes,
                                       const pugiutil::loc_data& loc_data) {
    pugi::xml_node CurSite;

    expect_only_children(Parent, {"site"}, loc_data);

    if (count_children(Parent, "site", loc_data) < 1) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                       "There are no sites corresponding to this tile: %s.\n", SubTile->name.c_str());
    }

    CurSite = Parent.first_child();
    while (CurSite) {
        check_node(CurSite, "site", loc_data);

        expect_only_attributes(CurSite, {"pb_type", "pin_mapping"}, loc_data);
        /* Load equivalent site name */
        auto Prop = std::string(get_attribute(CurSite, "pb_type", loc_data).value());

        auto LogicalBlockType = get_type_by_name<t_logical_block_type>(Prop.c_str(), LogicalBlockTypes);

        auto pin_mapping = get_attribute(CurSite, "pin_mapping", loc_data, ReqOpt::OPTIONAL).as_string("direct");

        if (0 == strcmp(pin_mapping, "custom")) {
            // Pin mapping between Tile and Pb Type is user-defined
            ProcessEquivalentSiteCustomConnection(CurSite, SubTile, PhysicalTileType, LogicalBlockType, Prop, loc_data);
        } else if (0 == strcmp(pin_mapping, "direct")) {
            ProcessEquivalentSiteDirectConnection(CurSite, SubTile, PhysicalTileType, LogicalBlockType, loc_data);
        }

        if (0 == strcmp(LogicalBlockType->pb_type->name, Prop.c_str())) {
            SubTile->equivalent_sites.push_back(LogicalBlockType);

            check_port_direct_mappings(PhysicalTileType, SubTile, LogicalBlockType);
        }

        CurSite = CurSite.next_sibling(CurSite.name());
    }
}

static void ProcessEquivalentSiteDirectConnection(pugi::xml_node Parent,
                                                  t_sub_tile* SubTile,
                                                  t_physical_tile_type* PhysicalTileType,
                                                  t_logical_block_type* LogicalBlockType,
                                                  const pugiutil::loc_data& loc_data) {
    int num_pins = (int)SubTile->sub_tile_to_tile_pin_indices.size() / SubTile->capacity.total();

    if (num_pins != LogicalBlockType->pb_type->num_pins) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                       "Pin definition differ between site %s and tile %s. User-defined pin mapping is required.\n",
                       LogicalBlockType->pb_type->name,
                       SubTile->name.c_str());
    }

    vtr::bimap<t_logical_pin, t_physical_pin> directs_map;

    for (int npin = 0; npin < num_pins; npin++) {
        t_physical_pin physical_pin(npin);
        t_logical_pin logical_pin(npin);

        directs_map.insert(logical_pin, physical_pin);
    }

    PhysicalTileType->tile_block_pin_directs_map[LogicalBlockType->index][SubTile->index] = directs_map;
}

static void ProcessEquivalentSiteCustomConnection(pugi::xml_node Parent,
                                                  t_sub_tile* SubTile,
                                                  t_physical_tile_type* PhysicalTileType,
                                                  t_logical_block_type* LogicalBlockType,
                                                  const std::string& site_name,
                                                  const pugiutil::loc_data& loc_data) {
    pugi::xml_node CurDirect;

    expect_only_children(Parent, {"direct"}, loc_data);

    if (count_children(Parent, "direct", loc_data) < 1) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                       "There are no direct pin mappings between site %s and tile %s.\n", site_name.c_str(), SubTile->name.c_str());
    }

    vtr::bimap<t_logical_pin, t_physical_pin> directs_map;

    CurDirect = Parent.first_child();

    while (CurDirect) {
        check_node(CurDirect, "direct", loc_data);

        expect_only_attributes(CurDirect, {"from", "to"}, loc_data);

        std::string from, to;
        // `from` attribute is relative to the physical tile pins
        from = std::string(get_attribute(CurDirect, "from", loc_data).value());

        // `to` attribute is relative to the logical block pins
        to = std::string(get_attribute(CurDirect, "to", loc_data).value());

        auto from_pins = ProcessPinString<t_sub_tile*>(CurDirect, SubTile, from.c_str(), loc_data);
        auto to_pins = ProcessPinString<t_logical_block_type_ptr>(CurDirect, LogicalBlockType, to.c_str(), loc_data);

        // Checking that the number of pins is exactly the same
        if (from_pins.second - from_pins.first != to_pins.second - to_pins.first) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                           "The number of pins specified in the direct pin mapping is "
                           "not equivalent for Physical Tile %s and Logical Block %s.\n",
                           SubTile->name.c_str(), LogicalBlockType->name.c_str());
        }

        int num_pins = from_pins.second - from_pins.first;
        for (int i = 0; i < num_pins; i++) {
            t_physical_pin physical_pin(from_pins.first + i);
            t_logical_pin logical_pin(to_pins.first + i);

            auto result = directs_map.insert(logical_pin, physical_pin);
            if (!result.second) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Parent),
                               "Duplicate logical pin (%d) to physical pin (%d) mappings found for "
                               "Physical Tile %s and Logical Block %s.\n",
                               logical_pin.pin, physical_pin.pin, SubTile->name.c_str(), LogicalBlockType->name.c_str());
            }
        }

        CurDirect = CurDirect.next_sibling(CurDirect.name());
    }

    PhysicalTileType->tile_block_pin_directs_map[LogicalBlockType->index][SubTile->index] = directs_map;
}

static void ProcessPinLocations(pugi::xml_node Locations,
                                t_physical_tile_type* PhysicalTileType,
                                t_sub_tile* SubTile,
                                t_pin_locs* pin_locs,
                                const pugiutil::loc_data& loc_data,
                                const int num_of_avail_layer) {
    pugi::xml_node Cur;
    const char* Prop;
    enum e_pin_location_distr distribution;

    if (Locations) {
        expect_only_attributes(Locations, {"pattern"}, loc_data);

        Prop = get_attribute(Locations, "pattern", loc_data).value();
        if (strcmp(Prop, "spread") == 0) {
            distribution = e_pin_location_distr::SPREAD;
        } else if (strcmp(Prop, "perimeter") == 0) {
            distribution = e_pin_location_distr::PERIMETER;
        } else if (strcmp(Prop, "spread_inputs_perimeter_outputs") == 0) {
            distribution = e_pin_location_distr::SPREAD_INPUTS_PERIMETER_OUTPUTS;
        } else if (strcmp(Prop, "custom") == 0) {
            distribution = e_pin_location_distr::CUSTOM;
        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                           "%s is an invalid pin location pattern.\n", Prop);
        }
    } else {
        distribution = e_pin_location_distr::SPREAD;
        Prop = "spread";
    }

    if (pin_locs->is_distribution_set()) {
        if (pin_locs->distribution != distribution) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                           "Sub Tile %s has a different pin location pattern (%s) with respect "
                           "to the sibling sub tiles",
                           SubTile->name.c_str(), Prop);
        }
    } else {
        pin_locs->distribution = distribution;
        pin_locs->set_distribution();
    }

    const int sub_tile_index = SubTile->index;

    /* Load the pin locations */
    if (distribution == e_pin_location_distr::CUSTOM) {
        expect_only_children(Locations, {"loc"}, loc_data);
        Cur = Locations.first_child();
        //check for duplications ([0..3][0..type->width-1][0..type->height-1][0..num_of_avail_layer-1])
        std::set<std::tuple<e_side, int, int, int>> seen_sides;
        while (Cur) {
            check_node(Cur, "loc", loc_data);

            expect_only_attributes(Cur, {"side", "xoffset", "yoffset", "layer_offset"}, loc_data);

            /* Get offset (height, width, layer) */
            int x_offset = get_attribute(Cur, "xoffset", loc_data, ReqOpt::OPTIONAL).as_int(0);
            int y_offset = get_attribute(Cur, "yoffset", loc_data, ReqOpt::OPTIONAL).as_int(0);
            int layer_offset = pugiutil::get_attribute(Cur, "layer_offset", loc_data, ReqOpt::OPTIONAL).as_int(0);

            /* Get side */
            e_side side = TOP;
            Prop = get_attribute(Cur, "side", loc_data).value();
            if (0 == strcmp(Prop, "left")) {
                side = LEFT;
            } else if (0 == strcmp(Prop, "top")) {
                side = TOP;
            } else if (0 == strcmp(Prop, "right")) {
                side = RIGHT;
            } else if (0 == strcmp(Prop, "bottom")) {
                side = BOTTOM;
            } else {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                               "'%s' is not a valid side.\n", Prop);
            }

            if ((x_offset < 0) || (x_offset >= PhysicalTileType->width)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                               "'%d' is an invalid horizontal offset for type '%s' (must be within [0, %d]).\n",
                               x_offset, PhysicalTileType->name.c_str(), PhysicalTileType->width - 1);
            }
            if ((y_offset < 0) || (y_offset >= PhysicalTileType->height)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                               "'%d' is an invalid vertical offset for type '%s' (must be within [0, %d]).\n",
                               y_offset, PhysicalTileType->name.c_str(), PhysicalTileType->height - 1);
            }

            if ((layer_offset < 0) || layer_offset >= num_of_avail_layer) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                               "'%d' is an invalid layer offset for type '%s' (must be within [0, num_avail_layer-1]).\n",
                               y_offset, PhysicalTileType->name.c_str(), PhysicalTileType->height - 1);
            }

            //Check for duplicate side specifications, since the code below silently overwrites if there are duplicates
            auto side_offset = std::make_tuple(side, x_offset, y_offset, layer_offset);
            if (seen_sides.count(side_offset)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                               "Duplicate pin location side/offset specification."
                               " Only a single <loc> per side/xoffset/yoffset/layer_offset is permitted.\n");
            }
            seen_sides.insert(side_offset);

            /* Go through lists of pins */
            const std::vector<std::string> Tokens = vtr::split(Cur.child_value());
            int Count = (int)Tokens.size();
            if (Count > 0) {
                for (int pin = 0; pin < Count; ++pin) {
                    /* Store location assignment */
                    pin_locs->assignments[sub_tile_index][x_offset][y_offset][std::abs(layer_offset)][side].emplace_back(Tokens[pin].c_str());
                    /* Advance through list of pins in this location */
                }
            }
            Cur = Cur.next_sibling(Cur.name());
        }

        //Verify that all top-level pins have had their locations specified

        //Record all the specified pins
        std::map<std::string, std::set<int>> port_pins_with_specified_locations;
        for (int l = 0; l < num_of_avail_layer; ++l) {
            for (int w = 0; w < PhysicalTileType->width; ++w) {
                for (int h = 0; h < PhysicalTileType->height; ++h) {
                    for (e_side side : TOTAL_2D_SIDES) {
                        for (const std::string& token : pin_locs->assignments[sub_tile_index][w][h][l][side]) {
                            InstPort inst_port(token);

                            //A pin specification should contain only the block name, and not any instance count information
                            if (inst_port.instance_low_index() != InstPort::UNSPECIFIED || inst_port.instance_high_index() != InstPort::UNSPECIFIED) {
                                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                                               "Pin location specification '%s' should not contain an instance range (should only be the block name)",
                                               token.c_str());
                            }

                            //Check that the block name matches
                            if (inst_port.instance_name() != SubTile->name) {
                                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                                               "Mismatched sub tile name in pin location specification (expected '%s' was '%s')",
                                               SubTile->name.c_str(), inst_port.instance_name().c_str());
                            }

                            int pin_low_idx = inst_port.port_low_index();
                            int pin_high_idx = inst_port.port_high_index();

                            if (pin_low_idx == InstPort::UNSPECIFIED && pin_high_idx == InstPort::UNSPECIFIED) {
                                //Empty range, so full port

                                //Find the matching pb type to get the total number of pins
                                const t_physical_tile_port* port = nullptr;
                                for (const auto& tmp_port : SubTile->ports) {
                                    if (tmp_port.name == inst_port.port_name()) {
                                        port = &tmp_port;
                                        break;
                                    }
                                }

                                if (port) {
                                    pin_low_idx = 0;
                                    pin_high_idx = port->num_pins - 1;
                                } else {
                                    archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                                                   "Failed to find port named '%s' on block '%s'",
                                                   inst_port.port_name().c_str(), SubTile->name.c_str());
                                }
                            }
                            VTR_ASSERT(pin_low_idx >= 0);
                            VTR_ASSERT(pin_high_idx >= 0);

                            for (int ipin = pin_low_idx; ipin <= pin_high_idx; ++ipin) {
                                //Record that the pin has its location specified
                                port_pins_with_specified_locations[inst_port.port_name()].insert(ipin);
                            }
                        }
                    }
                }
            }
        }

        //Check for any pins missing location specs
        for (const auto& port : SubTile->ports) {
            for (int ipin = 0; ipin < port.num_pins; ++ipin) {
                if (!port_pins_with_specified_locations[port.name].count(ipin)) {
                    //Missing
                    archfpga_throw(loc_data.filename_c_str(), loc_data.line(Locations),
                                   "Pin '%s.%s[%d]' has no pin location specified (a location is required for pattern=\"custom\")",
                                   SubTile->name.c_str(), port.name, ipin);
                }
            }
        }
    } else if (Locations) {
        //Non-custom pin locations. There should be no child tags
        expect_child_node_count(Locations, 0, loc_data);
    }
}

static void ProcessSubTiles(pugi::xml_node Node,
                            t_physical_tile_type* PhysicalTileType,
                            std::vector<t_logical_block_type>& LogicalBlockTypes,
                            std::vector<t_segment_inf>& segments,
                            const t_default_fc_spec& arch_def_fc,
                            const pugiutil::loc_data& loc_data,
                            const int num_of_avail_layer) {
    pugi::xml_node CurSubTile;
    pugi::xml_node Cur;

    unsigned long int num_sub_tiles = count_children(Node, "sub_tile", loc_data);
    unsigned long int width = PhysicalTileType->width;
    unsigned long int height = PhysicalTileType->height;
    unsigned long int num_sides = 4;

    t_pin_locs pin_locs;
    pin_locs.assignments.resize({num_sub_tiles, width, height, (unsigned long int)num_of_avail_layer, num_sides});

    if (num_sub_tiles == 0) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                       "No sub tile found for the Physical Tile %s.\n"
                       "At least one sub tile is needed to correctly describe the Physical Tile.\n",
                       PhysicalTileType->name.c_str());
    }

    // used to find duplicate subtile names
    std::set<std::string> sub_tile_names;

    // used to assign indices to subtiles
    int subtile_index = 0;

    CurSubTile = get_first_child(Node, "sub_tile", loc_data);

    while (CurSubTile) {
        t_sub_tile SubTile;

        SubTile.index = subtile_index;

        expect_only_attributes(CurSubTile, {"name", "capacity"}, loc_data);

        /* Load type name */
        const char* name = get_attribute(CurSubTile, "name", loc_data).value();

        //Check Sub Tile name duplicates
        auto [_, success] = sub_tile_names.insert(name);
        if (!success) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Cur),
                           "Duplicate Sub Tile names in tile '%s': Sub Tile'%s'\n",
                           PhysicalTileType->name.c_str(), name);
        }

        SubTile.name = name;

        /* Load properties */
        int capacity = get_attribute(CurSubTile, "capacity", loc_data, ReqOpt::OPTIONAL).as_int(1);
        SubTile.capacity.set(PhysicalTileType->capacity, PhysicalTileType->capacity + capacity - 1);
        PhysicalTileType->capacity += capacity;

        /* Process sub tile port definitions */
        const auto pin_counts = ProcessSubTilePorts(CurSubTile, &SubTile, loc_data);

        /* Map Sub Tile physical pins with the Physical Tile Type physical pins.
         * This takes into account the capacity of each sub tiles to add the correct offset.
         */
        for (int ipin = 0; ipin < capacity * pin_counts.total(); ipin++) {
            SubTile.sub_tile_to_tile_pin_indices.push_back(PhysicalTileType->num_pins + ipin);
        }

        SubTile.num_phy_pins = pin_counts.total() * capacity;

        /* Assign pin counts to the Physical Tile Type */
        PhysicalTileType->num_input_pins += capacity * pin_counts.input;
        PhysicalTileType->num_output_pins += capacity * pin_counts.output;
        PhysicalTileType->num_clock_pins += capacity * pin_counts.clock;
        PhysicalTileType->num_pins += capacity * pin_counts.total();
        PhysicalTileType->num_inst_pins += pin_counts.total();

        /* Assign drivers and receivers count to Physical Tile Type */
        PhysicalTileType->num_receivers += capacity * pin_counts.input;
        PhysicalTileType->num_drivers += capacity * pin_counts.output;

        Cur = get_single_child(CurSubTile, "pinlocations", loc_data, ReqOpt::OPTIONAL);
        ProcessPinLocations(Cur, PhysicalTileType, &SubTile, &pin_locs, loc_data, num_of_avail_layer);

        /* Load Fc */
        Cur = get_single_child(CurSubTile, "fc", loc_data, ReqOpt::OPTIONAL);
        Process_Fc(Cur, PhysicalTileType, &SubTile, pin_counts, segments, arch_def_fc, loc_data);

        //Load equivalent sites information
        Cur = get_single_child(CurSubTile, "equivalent_sites", loc_data, ReqOpt::REQUIRED);
        ProcessTileEquivalentSites(Cur, &SubTile, PhysicalTileType, LogicalBlockTypes, loc_data);

        PhysicalTileType->sub_tiles.push_back(SubTile);

        subtile_index++;

        CurSubTile = CurSubTile.next_sibling(CurSubTile.name());
    }

    // Initialize pinloc data structure.
    int num_pins = PhysicalTileType->num_pins;
    PhysicalTileType->pinloc.resize({width, height, num_sides}, std::vector<bool>(num_pins, false));

    setup_pin_classes(PhysicalTileType);
    LoadPinLoc(Cur, PhysicalTileType, &pin_locs, loc_data, num_of_avail_layer);
}

/* Takes in node pointing to <typelist> and loads all the
 * child type objects. */
static void ProcessComplexBlocks(pugi::xml_node Node,
                                 std::vector<t_logical_block_type>& LogicalBlockTypes,
                                 const t_arch& arch,
                                 const bool timing_enabled,
                                 const pugiutil::loc_data& loc_data) {
    pugi::xml_node CurBlockType;
    pugi::xml_node Cur;

    // used to find duplicate pb_types names
    std::set<std::string> pb_type_descriptors;

    /* Alloc the type list. Need one additional t_type_descriptors:
     * 1: empty pseudo-type
     */
    t_logical_block_type EMPTY_LOGICAL_BLOCK_TYPE = get_empty_logical_type();
    EMPTY_LOGICAL_BLOCK_TYPE.index = 0;
    LogicalBlockTypes.push_back(EMPTY_LOGICAL_BLOCK_TYPE);

    /* Process the types */
    int index = 1; /* Skip over 'empty' type */

    CurBlockType = Node.first_child();
    while (CurBlockType) {
        int pb_type_idx = 0;

        check_node(CurBlockType, "pb_type", loc_data);

        t_logical_block_type LogicalBlockType;

        expect_only_attributes(CurBlockType, {"name"}, loc_data);

        /* Load type name */
        auto Prop = get_attribute(CurBlockType, "name", loc_data).value();
        LogicalBlockType.name = Prop;

        auto [_, success] = pb_type_descriptors.insert(LogicalBlockType.name);
        if (!success) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(CurBlockType),
                           "Duplicate pb_type descriptor name: '%s'.\n", LogicalBlockType.name.c_str());
        }

        /* Load pb_type info to assign to the Logical Block Type */
        LogicalBlockType.pb_type = new t_pb_type;
        LogicalBlockType.pb_type->name = vtr::strdup(LogicalBlockType.name.c_str());
        ProcessPb_Type(CurBlockType, LogicalBlockType.pb_type, nullptr, timing_enabled, arch, loc_data, pb_type_idx);

        LogicalBlockType.index = index;

        /* Type fully read */
        ++index;

        /* Push newly created Types to corresponding vectors */
        LogicalBlockTypes.push_back(LogicalBlockType);

        /* Free this node and get its next sibling node */
        CurBlockType = CurBlockType.next_sibling(CurBlockType.name());
    }
}

static std::vector<t_segment_inf> ProcessSegments(pugi::xml_node Parent,
                                                  const std::vector<t_arch_switch_inf>& switches,
                                                  const bool timing_enabled,
                                                  const bool switchblocklist_required,
                                                  const pugiutil::loc_data& loc_data) {
    const char* tmp;

    std::vector<t_segment_inf> Segs;

    pugi::xml_node SubElem;
    pugi::xml_node Node;

    /* Count the number of segs and check they are in fact
     * of segment elements. */
    int NumSegs = count_children(Parent, "segment", loc_data);

    /* Alloc segment list */
    if (NumSegs > 0) {
        Segs.resize(NumSegs);
    }

    /* Load the segments. */
    Node = get_first_child(Parent, "segment", loc_data);

    bool x_axis_seg_found = false; /*Flags to see if we have any x-directed segment type specified*/
    bool y_axis_seg_found = false; /*Flags to see if we have any y-directed segment type specified*/

    for (int i = 0; i < NumSegs; ++i) {
        /* Get segment name */
        tmp = get_attribute(Node, "name", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (tmp) {
            Segs[i].name = std::string(tmp);
        } else {
            /* if swich block is "custom", then you have to provide a name for segment */
            if (switchblocklist_required) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                               "No name specified for the segment #%d.\n", i);
            }
            /* set name to default: "unnamed_segment_" << i */
            std::stringstream ss;
            ss << "unnamed_segment_" << i;
            std::string dummy = ss.str();
            tmp = dummy.c_str();
            Segs[i].name = std::string(tmp);
        }

        /* Get segment length */
        int length = 1; /* DEFAULT */
        tmp = get_attribute(Node, "length", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (tmp) {
            if (strcmp(tmp, "longline") == 0) {
                Segs[i].longline = true;
            } else {
                length = vtr::atoi(tmp);
            }
        }
        Segs[i].length = length;

        /* Get the frequency */
        Segs[i].frequency = 1; /* DEFAULT */
        tmp = get_attribute(Node, "freq", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (tmp) {
            Segs[i].frequency = (int)(atof(tmp) * MAX_CHANNEL_WIDTH);
        }

        /* Get timing info */
        ReqOpt TIMING_ENABLE_REQD = BoolToReqOpt(timing_enabled);
        Segs[i].Rmetal = get_attribute(Node, "Rmetal", loc_data, TIMING_ENABLE_REQD).as_float(0);
        Segs[i].Cmetal = get_attribute(Node, "Cmetal", loc_data, TIMING_ENABLE_REQD).as_float(0);

        /*Get parallel axis*/

        Segs[i].parallel_axis = BOTH_AXIS; /*DEFAULT value if no axis is specified*/
        tmp = get_attribute(Node, "axis", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);

        if (tmp) {
            if (strcmp(tmp, "x") == 0) {
                Segs[i].parallel_axis = X_AXIS;
                x_axis_seg_found = true;
            } else if (strcmp(tmp, "y") == 0) {
                Segs[i].parallel_axis = Y_AXIS;
                y_axis_seg_found = true;
            } else {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node), "Unsopported parralel axis type: %s\n", tmp);
            }
        } else {
            x_axis_seg_found = true;
            y_axis_seg_found = true;
        }

        /*Get segment resource type*/
        tmp = get_attribute(Node, "res_type", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);

        if (tmp) {
            auto it = std::find(RES_TYPE_STRING.begin(), RES_TYPE_STRING.end(), tmp);
            if (it != RES_TYPE_STRING.end()) {
                Segs[i].res_type = static_cast<SegResType>(std::distance(RES_TYPE_STRING.begin(), it));
            } else {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node), "Unsopported segment res_type: %s\n", tmp);
            }
        }

        /* Get Power info */
        /*
         * (*Segs)[i].Cmetal_per_m = get_attribute(Node, "Cmetal_per_m", false,
         * 0.);*/

        //Set of expected subtags (exact subtags are dependent on parameters)
        std::vector<std::string> expected_subtags;

        if (!Segs[i].longline) {
            //Long line doesn't accpet <sb> or <cb> since it assumes full population
            expected_subtags.emplace_back("sb");
            expected_subtags.emplace_back("cb");
        }

        /* Get the type */
        tmp = get_attribute(Node, "type", loc_data).value();
        if (0 == strcmp(tmp, "bidir")) {
            Segs[i].directionality = BI_DIRECTIONAL;

            //Bidir requires the following tags
            expected_subtags.emplace_back("wire_switch");
            expected_subtags.emplace_back("opin_switch");
        }

        else if (0 == strcmp(tmp, "unidir")) {
            Segs[i].directionality = UNI_DIRECTIONAL;

            //Unidir requires the following tags
            expected_subtags.emplace_back("mux");
            expected_subtags.emplace_back("mux_inter_die");
            //with the following two tags, we can allow the architecture file to define
            //different muxes with different delays for wires with different directions
            expected_subtags.emplace_back("mux_inc");
            expected_subtags.emplace_back("mux_dec");
        }

        else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                           "Invalid switch type '%s'.\n", tmp);
        }

        //Verify only expected sub-tags are found
        expect_only_children(Node, expected_subtags, loc_data);

        //Get the switch name for different dice wire and track connections
        SubElem = get_single_child(Node, "mux_inter_die", loc_data, ReqOpt::OPTIONAL);
        tmp = get_attribute(SubElem, "name", loc_data, ReqOpt::OPTIONAL).as_string("");
        if (strlen(tmp) != 0) {
            /* Match names */
            int switch_idx = find_switch_by_name(switches, tmp);
            if (switch_idx < 0) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem),
                               "'%s' is not a valid mux name.\n", tmp);
            }
            Segs[i].arch_inter_die_switch = switch_idx;
        }

        /* Get the wire and opin switches, or mux switch if unidir */
        if (UNI_DIRECTIONAL == Segs[i].directionality) {
            //Get the switch name for same die wire and track connections
            SubElem = get_single_child(Node, "mux", loc_data, ReqOpt::OPTIONAL);
            tmp = get_attribute(SubElem, "name", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);

            //check if <mux> tag is defined in the architecture, otherwise we should look for <mux_inc> and <mux_dec>
            if (tmp) {
                /* Match names */
                int switch_idx = find_switch_by_name(switches, tmp);
                if (switch_idx < 0) {
                    archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem),
                                   "'%s' is not a valid mux name.\n", tmp);
                }

                /* Unidir muxes must have the same switch
                 * for wire and opin fanin since there is
                 * really only the mux in unidir. */
                Segs[i].arch_wire_switch = switch_idx;
                Segs[i].arch_opin_switch = switch_idx;
            } else { //if a general mux is not defined, we should look for specific mux for each direction in the architecture file
                SubElem = get_single_child(Node, "mux_inc", loc_data, ReqOpt::OPTIONAL);
                tmp = get_attribute(SubElem, "name", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
                if (!tmp) {
                    archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem),
                                   "if mux is not specified in a wire segment, both mux_inc and mux_dec should be specified");
                } else {
                    /* Match names */
                    int switch_idx = find_switch_by_name(switches, tmp);
                    if (switch_idx < 0) {
                        archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem),
                                       "'%s' is not a valid mux name.\n", tmp);
                    }

                    /* Unidir muxes must have the same switch
                     * for wire and opin fanin since there is
                     * really only the mux in unidir. */
                    Segs[i].arch_wire_switch = switch_idx;
                    Segs[i].arch_opin_switch = switch_idx;
                }

                SubElem = get_single_child(Node, "mux_dec", loc_data, ReqOpt::OPTIONAL);
                tmp = get_attribute(SubElem, "name", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
                if (!tmp) {
                    archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem),
                                   "if mux is not specified in a wire segment, both mux_inc and mux_dec should be specified");
                } else {
                    /* Match names */
                    int switch_idx = find_switch_by_name(switches, tmp);
                    if (switch_idx < 0) {
                        archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem),
                                       "'%s' is not a valid mux name.\n", tmp);
                    }

                    /* Unidir muxes must have the same switch
                     * for wire and opin fanin since there is
                     * really only the mux in unidir. */
                    Segs[i].arch_wire_switch_dec = switch_idx;
                    Segs[i].arch_opin_switch_dec = switch_idx;
                }
            }
        } else {
            VTR_ASSERT(BI_DIRECTIONAL == Segs[i].directionality);
            SubElem = get_single_child(Node, "wire_switch", loc_data);
            tmp = get_attribute(SubElem, "name", loc_data).value();

            /* Match names */
            int switch_idx = find_switch_by_name(switches, tmp);
            if (switch_idx < 0) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem),
                               "'%s' is not a valid wire_switch name.\n", tmp);
            }
            Segs[i].arch_wire_switch = switch_idx;
            SubElem = get_single_child(Node, "opin_switch", loc_data);
            tmp = get_attribute(SubElem, "name", loc_data).value();

            /* Match names */
            switch_idx = find_switch_by_name(switches, tmp);
            if (switch_idx < 0) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem),
                               "'%s' is not a valid opin_switch name.\n", tmp);
            }
            Segs[i].arch_opin_switch = switch_idx;
        }

        /* Setup the CB list if they give one, otherwise use full */
        Segs[i].cb.resize(length);
        for (int j = 0; j < length; ++j) {
            Segs[i].cb[j] = true;
        }
        SubElem = get_single_child(Node, "cb", loc_data, ReqOpt::OPTIONAL);
        if (SubElem) {
            ProcessCB_SB(SubElem, Segs[i].cb, loc_data);
        }

        /* Setup the SB list if they give one, otherwise use full */
        Segs[i].sb.resize(length + 1);
        for (int j = 0; j < (length + 1); ++j) {
            Segs[i].sb[j] = true;
        }
        SubElem = get_single_child(Node, "sb", loc_data, ReqOpt::OPTIONAL);
        if (SubElem) {
            ProcessCB_SB(SubElem, Segs[i].sb, loc_data);
        }

        /*Store the index of this segment in Segs vector*/
        Segs[i].seg_index = i;
        /* Get next Node */
        Node = Node.next_sibling(Node.name());
    }
    /*We need at least one type of segment that applies to each of x- and y-directed wiring.*/

    if (!x_axis_seg_found || !y_axis_seg_found) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                       "Atleast one segment per-axis needs to get specified if no segments with non-specified (default) axis attribute exist.");
    }

    return Segs;
}

static void calculate_custom_SB_locations(const pugiutil::loc_data& loc_data, const pugi::xml_node& SubElem, const int grid_width, const int grid_height, t_switchblock_inf& sb) {
    auto startx_attr = get_attribute(SubElem, "startx", loc_data, ReqOpt::OPTIONAL);
    auto endx_attr = get_attribute(SubElem, "endx", loc_data, ReqOpt::OPTIONAL);

    auto starty_attr = get_attribute(SubElem, "starty", loc_data, ReqOpt::OPTIONAL);
    auto endy_attr = get_attribute(SubElem, "endy", loc_data, ReqOpt::OPTIONAL);

    auto repeatx_attr = get_attribute(SubElem, "repeatx", loc_data, ReqOpt::OPTIONAL);
    auto repeaty_attr = get_attribute(SubElem, "repeaty", loc_data, ReqOpt::OPTIONAL);

    auto incrx_attr = get_attribute(SubElem, "incrx", loc_data, ReqOpt::OPTIONAL);
    auto incry_attr = get_attribute(SubElem, "incry", loc_data, ReqOpt::OPTIONAL);

    //parse the values from the architecture file and fill out SB region information
    vtr::FormulaParser p;

    vtr::t_formula_data vars;
    vars.set_var_value("W", grid_width);
    vars.set_var_value("H", grid_height);

    sb.reg_x.start = startx_attr.empty() ? 0 : p.parse_formula(startx_attr.value(), vars);
    sb.reg_y.start = starty_attr.empty() ? 0 : p.parse_formula(starty_attr.value(), vars);

    sb.reg_x.end = endx_attr.empty() ? (grid_width - 1) : p.parse_formula(endx_attr.value(), vars);
    sb.reg_y.end = endy_attr.empty() ? (grid_height - 1) : p.parse_formula(endy_attr.value(), vars);

    sb.reg_x.repeat = repeatx_attr.empty() ? 0 : p.parse_formula(repeatx_attr.value(), vars);
    sb.reg_y.repeat = repeaty_attr.empty() ? 0 : p.parse_formula(repeaty_attr.value(), vars);

    sb.reg_x.incr = incrx_attr.empty() ? 1 : p.parse_formula(incrx_attr.value(), vars);
    sb.reg_y.incr = incry_attr.empty() ? 1 : p.parse_formula(incry_attr.value(), vars);
}

/* Processes the switchblocklist section from the xml architecture file.
 * See vpr/SRC/route/build_switchblocks.c for a detailed description of this
 * switch block format */
static void ProcessSwitchblocks(pugi::xml_node Parent, t_arch* arch, const pugiutil::loc_data& loc_data) {
    pugi::xml_node Node;
    pugi::xml_node SubElem;
    const char* tmp;

    /* get the number of switchblocks */
    int num_switchblocks = count_children(Parent, "switchblock", loc_data);
    arch->switchblocks.reserve(num_switchblocks);

    int layout_index = -1;
    for (layout_index = 0; layout_index < (int)arch->grid_layouts.size(); layout_index++) {
        if (arch->grid_layouts.at(layout_index).name == arch->device_layout) {
            //found the used layout
            break;
        }
    }

    /* read-in all switchblock data */
    Node = get_first_child(Parent, "switchblock", loc_data);
    for (int i_sb = 0; i_sb < num_switchblocks; i_sb++) {
        /* use a temp variable which will be assigned to switchblocks later */
        t_switchblock_inf sb;

        /* get name */
        tmp = get_attribute(Node, "name", loc_data).as_string(nullptr);
        if (tmp) {
            sb.name = tmp;
        }

        /* get type */
        tmp = get_attribute(Node, "type", loc_data).as_string(nullptr);
        if (tmp) {
            if (0 == strcmp(tmp, "bidir")) {
                sb.directionality = BI_DIRECTIONAL;
            } else if (0 == strcmp(tmp, "unidir")) {
                sb.directionality = UNI_DIRECTIONAL;
            } else {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node), "Unsopported switchblock type: %s\n", tmp);
            }
        }

        /* get the switchblock location */
        SubElem = get_single_child(Node, "switchblock_location", loc_data);
        tmp = get_attribute(SubElem, "type", loc_data).as_string(nullptr);
        if (tmp) {
            if (strcmp(tmp, "EVERYWHERE") == 0) {
                sb.location = e_sb_location::E_EVERYWHERE;
            } else if (strcmp(tmp, "PERIMETER") == 0) {
                sb.location = e_sb_location::E_PERIMETER;
            } else if (strcmp(tmp, "CORE") == 0) {
                sb.location = e_sb_location::E_CORE;
            } else if (strcmp(tmp, "CORNER") == 0) {
                sb.location = e_sb_location::E_CORNER;
            } else if (strcmp(tmp, "FRINGE") == 0) {
                sb.location = e_sb_location::E_FRINGE;
            } else if (strcmp(tmp, "XY_SPECIFIED") == 0) {
                sb.location = e_sb_location::E_XY_SPECIFIED;
            } else {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem), "unrecognized switchblock location: %s\n", tmp);
            }
        }

        /* get the switchblock coordinate only if sb.location is set to E_XY_SPECIFIED*/
        if (sb.location == e_sb_location::E_XY_SPECIFIED) {
            if (arch->device_layout == "auto") {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem), "Specifying SB locations for auto layout devices are not supported yet!\n");
            }
            expect_only_attributes(SubElem,
                                   {"x", "y", "type",
                                    "startx", "endx", "repeatx", "incrx",
                                    "starty", "endy", "repeaty", "incry"},
                                   loc_data);

            int grid_width = arch->grid_layouts.at(layout_index).width;
            int grid_height = arch->grid_layouts.at(layout_index).height;

            /* Absolute location that this SB must be applied to, -1 if not specified*/
            sb.x = get_attribute(SubElem, "x", loc_data, ReqOpt::OPTIONAL).as_int(-1);
            sb.y = get_attribute(SubElem, "y", loc_data, ReqOpt::OPTIONAL).as_int(-1);

            //check if the absolute value is within the device grid width and height
            if (sb.x >= grid_width || sb.y >= grid_height) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(SubElem),
                               "Location (%d,%d) is not valid within the grid! grid dimensions are: (%d,%d)\n", sb.x, sb.y, grid_width, grid_height);
            }

            /* if the the switchblock exact location is not specified and a region is specified within the architecture file,
             * we have to parse the region specification and apply the SB pattern to all the locations fall into the specified 
             * region based on device width and height.
             */
            if (sb.x == -1 && sb.y == -1) {
                calculate_custom_SB_locations(loc_data, SubElem, grid_width, grid_height, sb);
            }
        }

        /* get switchblock permutation functions */
        SubElem = get_first_child(Node, "switchfuncs", loc_data);
        read_sb_switchfuncs(SubElem, &sb, loc_data);

        read_sb_wireconns(arch->switches, Node, &sb, loc_data);

        /* run error checks on switch blocks */
        check_switchblock(&sb, arch);

        /* assign the sb to the switchblocks vector */
        arch->switchblocks.push_back(sb);

        Node = Node.next_sibling(Node.name());
    }
}

static void ProcessCB_SB(pugi::xml_node Node, std::vector<bool>& list, const pugiutil::loc_data& loc_data) {
    const char* tmp = nullptr;
    int i;
    int len = list.size();
    /* Check the type. We only support 'pattern' for now.
     * Should add frac back eventually. */
    tmp = get_attribute(Node, "type", loc_data).value();
    if (0 == strcmp(tmp, "pattern")) {
        i = 0;

        /* Get the content string */
        tmp = Node.child_value();
        while (*tmp) {
            switch (*tmp) {
                case ' ':
                case '\t':
                case '\n':
                    break;
                case 'T':
                case '1':
                    if (i >= len) {
                        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                                       "CB or SB depopulation is too long (%d). It should be %d symbols for CBs and %d symbols for SBs.\n",
                                       i, len - 1, len);
                    }
                    list[i] = true;
                    ++i;
                    break;
                case 'F':
                case '0':
                    if (i >= len) {
                        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                                       "CB or SB depopulation is too long (%d). It should be %d symbols for CBs and %d symbols for SBs.\n",
                                       i, len - 1, len);
                    }
                    list[i] = false;
                    ++i;
                    break;
                default:
                    archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                                   "Invalid character %c in CB or SB depopulation list.\n",
                                   *tmp);
            }
            ++tmp;
        }
        if (i < len) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                           "CB or SB depopulation is too short (%d). It should be %d symbols for CBs and %d symbols for SBs.\n",
                           i, len - 1, len);
        }
    }

    else {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                       "'%s' is not a valid type for specifying cb and sb depopulation.\n",
                       tmp);
    }
}

static std::vector<t_arch_switch_inf> ProcessSwitches(pugi::xml_node Parent,
                                                      const bool timing_enabled,
                                                      const pugiutil::loc_data& loc_data) {
    const char* type_name;
    const char* switch_name;
    ReqOpt TIMING_ENABLE_REQD = BoolToReqOpt(timing_enabled);

    pugi::xml_node Node;

    /* Count the children and check they are switches */
    int n_switches = count_children(Parent, "switch", loc_data);
    std::vector<t_arch_switch_inf> switches;

    /* Alloc switch list */
    if (n_switches > 0) {
        switches.resize(n_switches);
    }

    /* Load the switches. */
    Node = get_first_child(Parent, "switch", loc_data);
    for (int i = 0; i < n_switches; ++i) {
        t_arch_switch_inf& arch_switch = switches[i];

        switch_name = get_attribute(Node, "name", loc_data).value();

        /* Check if the switch has conflicts with any reserved names */
        if (0 == strcmp(switch_name, VPR_DELAYLESS_SWITCH_NAME)) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                           "Switch name '%s' is a reserved name for VPR internal usage! Please use another  name.\n",
                           switch_name);
        }

        type_name = get_attribute(Node, "type", loc_data).value();

        /* Check for switch name collisions */
        for (int j = 0; j < i; ++j) {
            if (0 == strcmp(switches[j].name.c_str(), switch_name)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                               "Two switches with the same name '%s' were found.\n",
                               switch_name);
            }
        }
        arch_switch.name = std::string(switch_name);

        /* Figure out the type of switch */
        /* As noted above, due to their configuration of pass transistors feeding into a buffer,
         * only multiplexers and tristate buffers have an internal capacitance element.         */

        SwitchType type = SwitchType::MUX;
        if (0 == strcmp(type_name, "mux")) {
            type = SwitchType::MUX;
            expect_only_attributes(Node, {"type", "name", "R", "Cin", "Cout", "Cinternal", "Tdel", "buf_size", "power_buf_size", "mux_trans_size"}, " with type '"s + type_name + "'"s, loc_data);

        } else if (0 == strcmp(type_name, "tristate")) {
            type = SwitchType::TRISTATE;
            expect_only_attributes(Node, {"type", "name", "R", "Cin", "Cout", "Cinternal", "Tdel", "buf_size", "power_buf_size"}, " with type '"s + type_name + "'"s, loc_data);

        } else if (0 == strcmp(type_name, "buffer")) {
            type = SwitchType::BUFFER;
            expect_only_attributes(Node, {"type", "name", "R", "Cin", "Cout", "Tdel", "buf_size", "power_buf_size"}, " with type '"s + type_name + "'"s, loc_data);

        } else if (0 == strcmp(type_name, "pass_gate")) {
            type = SwitchType::PASS_GATE;
            expect_only_attributes(Node, {"type", "name", "R", "Cin", "Cout", "Tdel"}, " with type '"s + type_name + "'"s, loc_data);

        } else if (0 == strcmp(type_name, "short")) {
            type = SwitchType::SHORT;
            expect_only_attributes(Node, {"type", "name", "R", "Cin", "Cout", "Tdel"}, " with type "s + type_name + "'"s, loc_data);
        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                           "Invalid switch type '%s'.\n", type_name);
        }
        arch_switch.set_type(type);

        arch_switch.R = get_attribute(Node, "R", loc_data, TIMING_ENABLE_REQD).as_float(0);

        ReqOpt COUT_REQD = TIMING_ENABLE_REQD;
        ReqOpt CIN_REQD = TIMING_ENABLE_REQD;
        // We have defined the Cinternal parameter as optional, so that the user may specify an
        // architecture without Cinternal without breaking the program flow.
        ReqOpt CINTERNAL_REQD = ReqOpt::OPTIONAL;

        if (arch_switch.type() == SwitchType::SHORT) {
            //Cin/Cout are optional on shorts, since they really only have one capacitance
            CIN_REQD = ReqOpt::OPTIONAL;
            COUT_REQD = ReqOpt::OPTIONAL;
        }
        arch_switch.Cin = get_attribute(Node, "Cin", loc_data, CIN_REQD).as_float(0);
        arch_switch.Cout = get_attribute(Node, "Cout", loc_data, COUT_REQD).as_float(0);
        arch_switch.Cinternal = get_attribute(Node, "Cinternal", loc_data, CINTERNAL_REQD).as_float(0);

        if (arch_switch.type() == SwitchType::MUX) {
            //Only muxes have mux transistors
            arch_switch.mux_trans_size = get_attribute(Node, "mux_trans_size", loc_data, ReqOpt::OPTIONAL).as_float(1);
        } else {
            arch_switch.mux_trans_size = 0.;
        }

        if (arch_switch.type() == SwitchType::SHORT
            || arch_switch.type() == SwitchType::PASS_GATE) {
            //No buffers
            arch_switch.buf_size_type = BufferSize::ABSOLUTE;
            arch_switch.buf_size = 0.;
            arch_switch.power_buffer_type = POWER_BUFFER_TYPE_ABSOLUTE_SIZE;
            arch_switch.power_buffer_size = 0.;
        } else {
            auto buf_size_attrib = get_attribute(Node, "buf_size", loc_data, ReqOpt::OPTIONAL);
            if (!buf_size_attrib || buf_size_attrib.as_string() == std::string("auto")) {
                arch_switch.buf_size_type = BufferSize::AUTO;
                arch_switch.buf_size = 0.;
            } else {
                arch_switch.buf_size_type = BufferSize::ABSOLUTE;
                arch_switch.buf_size = buf_size_attrib.as_float();
            }

            auto power_buf_size = get_attribute(Node, "power_buf_size", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
            if (power_buf_size == nullptr) {
                arch_switch.power_buffer_type = POWER_BUFFER_TYPE_AUTO;
            } else if (strcmp(power_buf_size, "auto") == 0) {
                arch_switch.power_buffer_type = POWER_BUFFER_TYPE_AUTO;
            } else {
                arch_switch.power_buffer_type = POWER_BUFFER_TYPE_ABSOLUTE_SIZE;
                arch_switch.power_buffer_size = (float)vtr::atof(power_buf_size);
            }

            arch_switch.intra_tile = false;
        }

        //Load the Tdel (which may be specified with sub-tags)
        ProcessSwitchTdel(Node, timing_enabled, arch_switch, loc_data);

        /* Get next switch element */
        Node = Node.next_sibling(Node.name());
    }

    return switches;
}

/* Processes the switch delay. Switch delay can be specified in two ways.
 * First way: switch delay is specified as a constant via the property Tdel in the switch node.
 * Second way: switch delay is specified as a function of the switch fan-in. In this
 * case, multiple nodes in the form
 *
 * <Tdel num_inputs="1" delay="3e-11"/>
 *
 * are specified as children of the switch node. In this case, Tdel
 * is not included as a property of the switch node (first way). */
static void ProcessSwitchTdel(pugi::xml_node Node, const bool timing_enabled, t_arch_switch_inf& arch_switch, const pugiutil::loc_data& loc_data) {
    /* check if switch node has the Tdel property */
    bool has_Tdel_prop = false;
    float Tdel_prop_value = get_attribute(Node, "Tdel", loc_data, ReqOpt::OPTIONAL).as_float(UNDEFINED);
    if (Tdel_prop_value != UNDEFINED) {
        has_Tdel_prop = true;
    }

    /* check if switch node has Tdel children */
    bool has_Tdel_children = false;
    int num_Tdel_children = count_children(Node, "Tdel", loc_data, ReqOpt::OPTIONAL);
    if (num_Tdel_children != 0) {
        has_Tdel_children = true;
    }

    /* delay should not be specified as a Tdel property AND a Tdel child */
    if (has_Tdel_prop && has_Tdel_children) {
        archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                       "Switch delay should be specified as EITHER a Tdel property OR as a child of the switch node, not both");
    }

    /* get pointer to the switch's Tdel map, then read-in delay data into this map */
    if (has_Tdel_prop) {
        /* delay specified as a constant */
        arch_switch.set_Tdel(t_arch_switch_inf::UNDEFINED_FANIN, Tdel_prop_value);
    } else if (has_Tdel_children) {
        /* Delay specified as a function of switch fan-in.
         * Go through each Tdel child, read-in num_inputs and the delay value.
         * Insert this info into the switch delay map */
        pugi::xml_node Tdel_child = get_first_child(Node, "Tdel", loc_data);
        std::set<int> seen_fanins;
        for (int ichild = 0; ichild < num_Tdel_children; ichild++) {
            int num_inputs = get_attribute(Tdel_child, "num_inputs", loc_data).as_int(0);
            float Tdel_value = get_attribute(Tdel_child, "delay", loc_data).as_float(0.);

            if (seen_fanins.count(num_inputs)) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Tdel_child),
                               "Tdel node specified num_inputs (%d) that has already been specified by another Tdel node", num_inputs);
            } else {
                arch_switch.set_Tdel(num_inputs, Tdel_value);
                seen_fanins.insert(num_inputs);
            }
            Tdel_child = Tdel_child.next_sibling(Tdel_child.name());
        }
    } else {
        /* No delay info specified for switch */
        if (timing_enabled) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                           "Switch should contain intrinsic delay information if timing is enabled");
        } else {
            /* set a default value */
            arch_switch.set_Tdel(t_arch_switch_inf::UNDEFINED_FANIN, 0.);
        }
    }
}

static std::vector<t_direct_inf> ProcessDirects(pugi::xml_node Parent,
                                                const std::vector<t_arch_switch_inf>& switches,
                                                const pugiutil::loc_data& loc_data) {
    /* Count the children and check they are direct connections */
    expect_only_children(Parent, {"direct"}, loc_data);
    int num_directs = count_children(Parent, "direct", loc_data);
    std::vector<t_direct_inf> directs(num_directs);

    /* Load the directs. */
    pugi::xml_node Node = get_first_child(Parent, "direct", loc_data);
    for (int i = 0; i < num_directs; ++i) {
        expect_only_attributes(Node, {"name", "from_pin", "to_pin", "x_offset", "y_offset", "z_offset", "switch_name", "from_side", "to_side"}, loc_data);

        const char* direct_name = get_attribute(Node, "name", loc_data).value();
        /* Check for direct name collisions */
        for (int j = 0; j < i; ++j) {
            if (directs[j].name == direct_name) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                               "Two directs with the same name '%s' were found.\n",
                               direct_name);
            }
        }
        directs[i].name = direct_name;

        /* Figure out the source pin and sink pin name */
        const char* from_pin_name = get_attribute(Node, "from_pin", loc_data).value();
        const char* to_pin_name = get_attribute(Node, "to_pin", loc_data).value();

        /* Check that to_pin and the from_pin are not the same */
        if (0 == strcmp(to_pin_name, from_pin_name)) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                           "The source pin and sink pin are the same: %s.\n",
                           to_pin_name);
        }
        directs[i].from_pin = from_pin_name;
        directs[i].to_pin = to_pin_name;

        directs[i].x_offset = get_attribute(Node, "x_offset", loc_data).as_int(0);
        directs[i].y_offset = get_attribute(Node, "y_offset", loc_data).as_int(0);
        directs[i].sub_tile_offset = get_attribute(Node, "z_offset", loc_data).as_int(0);

        std::string from_side_str = get_attribute(Node, "from_side", loc_data, ReqOpt::OPTIONAL).value();
        directs[i].from_side = string_to_side(from_side_str);
        std::string to_side_str = get_attribute(Node, "to_side", loc_data, ReqOpt::OPTIONAL).value();
        directs[i].to_side = string_to_side(to_side_str);

        //Set the optional switch type
        const char* switch_name = get_attribute(Node, "switch_name", loc_data, ReqOpt::OPTIONAL).as_string(nullptr);
        if (switch_name != nullptr) {
            //Look-up the user defined switch
            int switch_idx = find_switch_by_name(switches, switch_name);
            if (switch_idx < 0) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(Node),
                               "Could not find switch named '%s' in switch list.\n", switch_name);
            }
            directs[i].switch_type = switch_idx; //Save the correct switch index
        } else {
            //If not defined, use the delayless switch by default
            //TODO: find a better way of indicating this.  Ideally, we would
            //specify the delayless switch index here, but it does not appear
            //to be defined at this point.
            directs[i].switch_type = -1;
        }

        directs[i].line = loc_data.line(Node);
        /* Should I check that the direct chain offset is not greater than the chip? How? */

        /* Get next direct element */
        Node = Node.next_sibling(Node.name());
    }

    return directs;
}

static void ProcessClockMetalLayers(pugi::xml_node parent,
                                    std::unordered_map<std::string, t_metal_layer>& metal_layers,
                                    pugiutil::loc_data& loc_data) {
    std::vector<std::string> expected_attributes = {"name", "Rmetal", "Cmetal"};
    std::vector<std::string> expected_children = {"metal_layer"};

    pugi::xml_node metal_layers_parent = get_single_child(parent, "metal_layers", loc_data);
    int num_metal_layers = count_children(metal_layers_parent, "metal_layer", loc_data);

    pugi::xml_node curr_layer = get_first_child(metal_layers_parent, "metal_layer", loc_data);
    for (int i = 0; i < num_metal_layers; i++) {
        expect_only_children(metal_layers_parent, expected_children, loc_data);
        expect_only_attributes(curr_layer, expected_attributes, loc_data);

        // Get metal layer values: name, r_metal, and c_metal
        std::string name(get_attribute(curr_layer, "name", loc_data).value());
        t_metal_layer metal_layer;
        metal_layer.r_metal = get_attribute(curr_layer, "Rmetal", loc_data).as_float(0.);
        metal_layer.c_metal = get_attribute(curr_layer, "Cmetal", loc_data).as_float(0.);

        // Insert metal layer into map
        auto itter = metal_layers.find(name);
        if (itter != metal_layers.end()) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(curr_layer),
                           "Two metal layers with the same name '%s' were found.\n",
                           name.c_str());
        }
        metal_layers.insert({name, metal_layer});

        curr_layer = curr_layer.next_sibling(curr_layer.name());
    }
}

static void ProcessClockNetworks(pugi::xml_node parent,
                                 std::vector<t_clock_network_arch>& clock_networks,
                                 const std::vector<t_arch_switch_inf>& switches,
                                 pugiutil::loc_data& loc_data) {
    std::vector<std::string> expected_spine_attributes = {"name", "num_inst", "metal_layer", "starty", "endy", "x", "repeatx", "repeaty"};
    std::vector<std::string> expected_rib_attributes = {"name", "num_inst", "metal_layer", "startx", "endx", "y", "repeatx", "repeaty"};
    std::vector<std::string> expected_children = {"rib", "spine"};

    int num_clock_networks = count_children(parent, "clock_network", loc_data);
    pugi::xml_node curr_network = get_first_child(parent, "clock_network", loc_data);
    for (int i = 0; i < num_clock_networks; i++) {
        expect_only_children(curr_network, expected_children, loc_data);

        t_clock_network_arch clock_network;

        std::string name(get_attribute(curr_network, "name", loc_data).value());
        clock_network.name = name;
        clock_network.num_inst = get_attribute(curr_network, "num_inst", loc_data).as_int(0);
        bool is_supported_clock_type = false;
        pugi::xml_node curr_type;

        // Parse spine
        curr_type = get_single_child(curr_network, "spine", loc_data, ReqOpt::OPTIONAL);
        if (curr_type) {
            expect_only_attributes(curr_network, expected_spine_attributes, loc_data);

            is_supported_clock_type = true;
            clock_network.type = e_clock_type::SPINE;

            std::string metal_layer(get_attribute(curr_type, "metal_layer", loc_data).value());
            std::string starty(get_attribute(curr_type, "starty", loc_data).value());
            std::string endy(get_attribute(curr_type, "endy", loc_data).value());
            std::string x(get_attribute(curr_type, "x", loc_data).value());

            std::string repeatx;
            auto repeatx_attr = get_attribute(curr_type, "repeatx", loc_data, ReqOpt::OPTIONAL);
            if (repeatx_attr) {
                repeatx = repeatx_attr.value();
            } else {
                repeatx = "W";
            }
            std::string repeaty;
            auto repeaty_attr = get_attribute(curr_type, "repeaty", loc_data, ReqOpt::OPTIONAL);
            if (repeaty_attr) {
                repeaty = repeaty_attr.value();
            } else {
                repeaty = "H";
            }

            clock_network.metal_layer = metal_layer;
            clock_network.wire.start = starty;
            clock_network.wire.end = endy;
            clock_network.wire.position = x;
            clock_network.repeat.x = repeatx;
            clock_network.repeat.y = repeaty;

            ProcessClockSwitchPoints(curr_type, clock_network, switches, loc_data);
        }

        // Parse rib
        curr_type = get_single_child(curr_network, "rib", loc_data, ReqOpt::OPTIONAL);
        if (curr_type) {
            expect_only_attributes(curr_network, expected_spine_attributes, loc_data);

            is_supported_clock_type = true;
            clock_network.type = e_clock_type::RIB;

            std::string metal_layer(get_attribute(curr_type, "metal_layer", loc_data).value());
            std::string startx(get_attribute(curr_type, "startx", loc_data).value());
            std::string endx(get_attribute(curr_type, "endx", loc_data).value());
            std::string y(get_attribute(curr_type, "y", loc_data).value());

            std::string repeatx;
            auto repeatx_attr = get_attribute(curr_type, "repeatx", loc_data, ReqOpt::OPTIONAL);
            if (repeatx_attr) {
                repeatx = repeatx_attr.value();
            } else {
                repeatx = "W";
            }
            std::string repeaty;
            auto repeaty_attr = get_attribute(curr_type, "repeaty", loc_data, ReqOpt::OPTIONAL);
            if (repeaty_attr) {
                repeaty = repeaty_attr.value();
            } else {
                repeaty = "H";
            }

            clock_network.metal_layer = metal_layer;
            clock_network.wire.start = startx;
            clock_network.wire.end = endx;
            clock_network.wire.position = y;
            clock_network.repeat.x = repeatx;
            clock_network.repeat.y = repeaty;

            ProcessClockSwitchPoints(curr_type, clock_network, switches, loc_data);
        }

        // Currently their is only support for ribs and spines
        if (!is_supported_clock_type) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(curr_type),
                           "Found no supported clock network type for '%s' clock network.\n"
                           "Currently there is only support for rib and spine networks.\n",
                           name.c_str());
        }

        clock_networks.push_back(clock_network);
        curr_network = curr_network.next_sibling(curr_network.name());
    }
}

static void ProcessClockSwitchPoints(pugi::xml_node parent,
                                     t_clock_network_arch& clock_network,
                                     const std::vector<t_arch_switch_inf>& switches,
                                     pugiutil::loc_data& loc_data) {
    std::vector<std::string> expected_spine_drive_attributes = {"name", "type", "yoffset", "switch_name"};
    std::vector<std::string> expected_rib_drive_attributes = {"name", "type", "xoffset", "switch_name"};
    std::vector<std::string> expected_spine_tap_attributes = {"name", "type", "yoffset", "yincr"};
    std::vector<std::string> expected_rib_tap_attributes = {"name", "type", "xoffset", "xincr"};
    std::vector<std::string> expected_children = {"switch_point"};

    int num_clock_switches = count_children(parent, "switch_point", loc_data);
    pugi::xml_node curr_switch = get_first_child(parent, "switch_point", loc_data);

    //TODO: currently only supporting one drive and one tap. Should change to support
    //      multiple taps
    VTR_ASSERT(switches.size() != 2);

    //TODO: ensure switch name is unique for every switch of this clock network
    for (int i = 0; i < num_clock_switches; i++) {
        expect_only_children(curr_switch, expected_children, loc_data);

        std::string switch_type(get_attribute(curr_switch, "type", loc_data).value());
        if (switch_type == "drive") {
            t_clock_drive drive;

            std::string name(get_attribute(curr_switch, "name", loc_data).value());
            const char* offset;
            if (clock_network.type == e_clock_type::SPINE) {
                expect_only_attributes(curr_switch, expected_spine_drive_attributes, loc_data);
                offset = get_attribute(curr_switch, "yoffset", loc_data).value();
            } else {
                VTR_ASSERT(clock_network.type == e_clock_type::RIB);
                expect_only_attributes(curr_switch, expected_rib_drive_attributes, loc_data);
                offset = get_attribute(curr_switch, "xoffset", loc_data).value();
            }

            // get switch index
            const char* switch_name = get_attribute(curr_switch, "switch_name", loc_data).value();
            int switch_idx = find_switch_by_name(switches, switch_name);
            if (switch_idx < 0) {
                archfpga_throw(loc_data.filename_c_str(), loc_data.line(curr_switch),
                               "'%s' is not a valid switch name.\n", switch_name);
            }

            drive.name = name;
            drive.offset = offset;
            drive.arch_switch_idx = switch_idx;
            clock_network.drive = drive;

        } else if (switch_type == "tap") {
            t_clock_taps tap;

            std::string name(get_attribute(curr_switch, "name", loc_data).value());
            const char* offset;
            const char* increment;
            if (clock_network.type == e_clock_type::SPINE) {
                expect_only_attributes(curr_switch, expected_spine_tap_attributes, loc_data);
                offset = get_attribute(curr_switch, "yoffset", loc_data).value();
                increment = get_attribute(curr_switch, "yincr", loc_data).value();
            } else {
                VTR_ASSERT(clock_network.type == e_clock_type::RIB);
                expect_only_attributes(curr_switch, expected_rib_tap_attributes, loc_data);
                offset = get_attribute(curr_switch, "xoffset", loc_data).value();
                increment = get_attribute(curr_switch, "xincr", loc_data).value();
            }

            tap.name = name;
            tap.offset = offset;
            tap.increment = increment;
            clock_network.tap = tap;

        } else {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(curr_switch),
                           "Found unsupported switch type for '%s' clock network.\n"
                           "Currently there is only support for drive and tap switch types.\n",
                           clock_network.name.c_str());
        }

        curr_switch = curr_switch.next_sibling(curr_switch.name());
    }
}

static void ProcessClockRouting(pugi::xml_node parent,
                                std::vector<t_clock_connection_arch>& clock_connections,
                                const std::vector<t_arch_switch_inf>& switches,
                                pugiutil::loc_data& loc_data) {
    std::vector<std::string> expected_attributes = {"from", "to", "switch", "fc_val", "locationx", "locationy"};

    pugi::xml_node clock_routing_parent = get_single_child(parent, "clock_routing", loc_data);
    int num_routing_connections = count_children(clock_routing_parent, "tap", loc_data);

    pugi::xml_node curr_connection = get_first_child(clock_routing_parent, "tap", loc_data);
    for (int i = 0; i < num_routing_connections; i++) {
        expect_only_attributes(curr_connection, expected_attributes, loc_data);

        t_clock_connection_arch clock_connection;

        const char* from = get_attribute(curr_connection, "from", loc_data).value();
        const char* to = get_attribute(curr_connection, "to", loc_data).value();
        const char* switch_name = get_attribute(curr_connection, "switch", loc_data).value();
        const char* locationx = get_attribute(curr_connection, "locationx", loc_data, ReqOpt::OPTIONAL).value();
        const char* locationy = get_attribute(curr_connection, "locationy", loc_data, ReqOpt::OPTIONAL).value();
        float fc = get_attribute(curr_connection, "fc_val", loc_data).as_float(0.);

        int switch_idx = find_switch_by_name(switches, switch_name);
        if (switch_idx < 0) {
            archfpga_throw(loc_data.filename_c_str(), loc_data.line(curr_connection),
                           "'%s' is not a valid switch name.\n", switch_name);
        }

        clock_connection.from = from;
        clock_connection.to = to;
        clock_connection.arch_switch_idx = switch_idx;
        clock_connection.locationx = locationx;
        clock_connection.locationy = locationy;
        clock_connection.fc = fc;

        clock_connections.push_back(clock_connection);

        curr_connection = curr_connection.next_sibling(curr_connection.name());
    }
}

static void ProcessPower(pugi::xml_node parent,
                         t_power_arch* power_arch,
                         const pugiutil::loc_data& loc_data) {
    pugi::xml_node Cur;

    /* Get the local interconnect capacitances */
    power_arch->local_interc_factor = 0.5;
    Cur = get_single_child(parent, "local_interconnect", loc_data, ReqOpt::OPTIONAL);
    if (Cur) {
        power_arch->C_wire_local = get_attribute(Cur, "C_wire", loc_data, ReqOpt::OPTIONAL).as_float(0.);
        power_arch->local_interc_factor = get_attribute(Cur, "factor", loc_data, ReqOpt::OPTIONAL).as_float(0.5);
    }

    /* Get logical effort factor */
    power_arch->logical_effort_factor = 4.0;
    Cur = get_single_child(parent, "buffers", loc_data, ReqOpt::OPTIONAL);
    if (Cur) {
        power_arch->logical_effort_factor = get_attribute(Cur,
                                                          "logical_effort_factor", loc_data)
                                                .as_float(0);
        ;
    }

    /* Get SRAM Size */
    power_arch->transistors_per_SRAM_bit = 6.0;
    Cur = get_single_child(parent, "sram", loc_data, ReqOpt::OPTIONAL);
    if (Cur) {
        power_arch->transistors_per_SRAM_bit = get_attribute(Cur,
                                                             "transistors_per_bit", loc_data)
                                                   .as_float(0);
    }

    /* Get Mux transistor size */
    power_arch->mux_transistor_size = 1.0;
    Cur = get_single_child(parent, "mux_transistor_size", loc_data, ReqOpt::OPTIONAL);
    if (Cur) {
        power_arch->mux_transistor_size = get_attribute(Cur,
                                                        "mux_transistor_size", loc_data)
                                              .as_float(0);
    }

    /* Get FF size */
    power_arch->FF_size = 1.0;
    Cur = get_single_child(parent, "FF_size", loc_data, ReqOpt::OPTIONAL);
    if (Cur) {
        power_arch->FF_size = get_attribute(Cur, "FF_size", loc_data).as_float(0);
    }

    /* Get LUT transistor size */
    power_arch->LUT_transistor_size = 1.0;
    Cur = get_single_child(parent, "LUT_transistor_size", loc_data, ReqOpt::OPTIONAL);
    if (Cur) {
        power_arch->LUT_transistor_size = get_attribute(Cur,
                                                        "LUT_transistor_size", loc_data)
                                              .as_float(0);
    }
}

/* Get the clock architecture */
static void ProcessClocks(pugi::xml_node Parent, t_clock_arch* clocks, const pugiutil::loc_data& loc_data) {
    pugi::xml_node Node;
    const char* tmp;

    clocks->num_global_clocks = count_children(Parent, "clock", loc_data, ReqOpt::OPTIONAL);

    /* Alloc the clockdetails */
    clocks->clock_inf = nullptr;
    if (clocks->num_global_clocks > 0) {
        clocks->clock_inf = (t_clock_network*)vtr::malloc(clocks->num_global_clocks * sizeof(t_clock_network));
        memset(clocks->clock_inf, 0,
               clocks->num_global_clocks * sizeof(t_clock_network));
    }

    /* Load the clock info. */
    Node = get_first_child(Parent, "clock", loc_data);
    for (int i = 0; i < clocks->num_global_clocks; ++i) {
        tmp = get_attribute(Node, "buffer_size", loc_data).value();
        if (strcmp(tmp, "auto") == 0) {
            clocks->clock_inf[i].autosize_buffer = true;
        } else {
            clocks->clock_inf[i].autosize_buffer = false;
            clocks->clock_inf[i].buffer_size = (float)atof(tmp);
        }

        clocks->clock_inf[i].C_wire = get_attribute(Node, "C_wire", loc_data).as_float(0);

        /* get the next clock item */
        Node = Node.next_sibling(Node.name());
    }
}

std::string inst_port_to_port_name(std::string inst_port) {
    auto pos = inst_port.find('.');
    if (pos != std::string::npos) {
        return inst_port.substr(pos + 1);
    }
    return inst_port;
}

static bool attribute_to_bool(const pugi::xml_node node,
                              const pugi::xml_attribute attr,
                              const pugiutil::loc_data& loc_data) {
    if (attr.value() == std::string("1")) {
        return true;
    } else if (attr.value() == std::string("0")) {
        return false;
    } else {
        bad_attribute_value(attr, node, loc_data, {"0", "1"});
    }

    return false;
}

static int find_switch_by_name(const std::vector<t_arch_switch_inf>& switches, std::string_view switch_name) {
    for (int iswitch = 0; iswitch < (int)switches.size(); ++iswitch) {
        const t_arch_switch_inf& arch_switch = switches[iswitch];
        if (arch_switch.name == switch_name) {
            return iswitch;
        }
    }

    return -1;
}

static e_side string_to_side(const std::string& side_str) {
    e_side side = NUM_2D_SIDES;
    if (side_str.empty()) {
        side = NUM_2D_SIDES;
    } else if (side_str == "left") {
        side = LEFT;
    } else if (side_str == "right") {
        side = RIGHT;
    } else if (side_str == "top") {
        side = TOP;
    } else if (side_str == "bottom") {
        side = BOTTOM;
    } else {
        archfpga_throw(__FILE__, __LINE__,
                       "Invalid side specification");
    }
    return side;
}

template<typename T>
static T* get_type_by_name(std::string_view type_name, std::vector<T>& types) {
    for (auto& type : types) {
        if (type.name == type_name) {
            return &type;
        }
    }

    archfpga_throw(__FILE__, __LINE__,
                   "Could not find type: %s\n", type_name);
}