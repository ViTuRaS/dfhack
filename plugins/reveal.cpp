#include <stdint.h>
#include <iostream>
#include <map>
#include <vector>
#include <dfhack/Core.h>
#include <dfhack/Console.h>
#include <dfhack/Export.h>
#include <dfhack/PluginManager.h>
#include <dfhack/modules/Maps.h>
#include <dfhack/modules/World.h>

using namespace DFHack;

/*
 * Anything that might reveal Hell is unsafe.
 */
bool isSafe(uint32_t x, uint32_t y, uint32_t z, DFHack::Maps *Maps)
{
    DFHack::t_feature *local_feature = NULL;
    DFHack::t_feature *global_feature = NULL;
    // get features of block
    // error -> obviously not safe to manipulate
    if(!Maps->ReadFeatures(x,y,z,&local_feature,&global_feature))
    {
        return false;
    }
    // Adamantine tubes and temples lead to Hell
    if (local_feature && local_feature->type != DFHack::feature_Other)
        return false;
    // And Hell *is* Hell.
    if (global_feature && global_feature->type == DFHack::feature_Underworld)
        return false;
    // otherwise it's safe.
    return true;
}

struct hideblock
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint8_t hiddens [16][16];
};

// the saved data. we keep map size to check if things still match
uint32_t x_max, y_max, z_max;
std::vector <hideblock> hidesaved;

enum revealstate
{
    NOT_REVEALED,
    REVEALED,
    SAFE_REVEALED
};

revealstate revealed = NOT_REVEALED;

DFhackCExport command_result reveal(DFHack::Core * c, std::vector<std::string> & params);
DFhackCExport command_result unreveal(DFHack::Core * c, std::vector<std::string> & params);
DFhackCExport command_result revtoggle(DFHack::Core * c, std::vector<std::string> & params);

DFhackCExport const char * plugin_name ( void )
{
    return "reveal";
}

DFhackCExport command_result plugin_init ( Core * c, std::vector <PluginCommand> &commands)
{
    commands.clear();
    commands.push_back(PluginCommand("reveal","Reveal the map. 'reveal hell' will also reveal hell.",reveal));
    commands.push_back(PluginCommand("unreveal","Revert the map to its previous state.",unreveal));
    commands.push_back(PluginCommand("revtoggle","Reveal/unreveal depending on state.",revtoggle));
    return CR_OK;
}

DFhackCExport command_result plugin_onupdate ( Core * c )
{
    // if the map is revealed and we're in fortress mode, force the game to pause.
    if(revealed == REVEALED)
    {
        DFHack::World *World =c->getWorld();
        t_gamemodes gm;
        World->ReadGameMode(gm);
        if(gm.g_mode == GAMEMODE_DWARF)
        {
            World->SetPauseState(true);
        }
    }
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown ( Core * c )
{
    return CR_OK;
}

DFhackCExport command_result reveal(DFHack::Core * c, std::vector<std::string> & params)
{
    bool no_hell = true;
    if(params.size() && params[0] == "hell")
    {
        no_hell = false;
    }
    Console & con = c->con;
    if(revealed != NOT_REVEALED)
    {
        con << "Map is already revealed or this is a different map." << std::endl;
        return CR_FAILURE;
    }

    c->Suspend();
    DFHack::Maps *Maps =c->getMaps();
    DFHack::World *World =c->getWorld();
    t_gamemodes gm;
    World->ReadGameMode(gm);
    if(gm.g_mode != GAMEMODE_DWARF)
    {
        con << "Only in fortress mode." << std::endl;
        c->Resume();
        return CR_FAILURE;
    }
    if(!Maps->Start())
    {
        con << "Can't init map." << std::endl;
        c->Resume();
        return CR_FAILURE;
    }

    if(no_hell && !Maps->StartFeatures())
    {
        con << "Unable to read local features; can't reveal map safely" << std::endl;
        c->Resume();
        return CR_FAILURE;
    }

    Maps->getSize(x_max,y_max,z_max);
    hidesaved.reserve(x_max * y_max * z_max);
    for(uint32_t x = 0; x< x_max;x++)
    {
        for(uint32_t y = 0; y< y_max;y++)
        {
            for(uint32_t z = 0; z< z_max;z++)
            {
                df_block *block = Maps->getBlock(x,y,z);
                if(block)
                {
                    // in 'no-hell'/'safe' mode, don't reveal blocks with hell and adamantine
                    if (no_hell && !isSafe(x, y, z, Maps))
                        continue;
                    hideblock hb;
                    hb.x = x;
                    hb.y = y;
                    hb.z = z;
                    DFHack::designations40d & designations = block->designation;
                    // for each tile in block
                    for (uint32_t i = 0; i < 16;i++) for (uint32_t j = 0; j < 16;j++)
                    {
                        // save hidden state of tile
                        hb.hiddens[i][j] = designations[i][j].bits.hidden;
                        // set to revealed
                        designations[i][j].bits.hidden = 0;
                    }
                    hidesaved.push_back(hb);
                }
            }
        }
    }
    if(no_hell)
    {
        revealed = SAFE_REVEALED;
    }
    else
    {
        revealed = REVEALED;
        World->SetPauseState(true);
    }
    c->Resume();
    con << "Map revealed." << std::endl;
    if(!no_hell)
        con << "Unpausing can unleash the forces of hell, so it has been temporarily disabled." << std::endl;
    con << "Run 'unreveal' to revert to previous state." << std::endl;
    return CR_OK;
}

DFhackCExport command_result unreveal(DFHack::Core * c, std::vector<std::string> & params)
{
    Console & con = c->con;
    if(!revealed)
    {
        con << "There's nothing to revert!" << std::endl;
        return CR_FAILURE;
    }
    c->Suspend();
    DFHack::Maps *Maps =c->getMaps();
    DFHack::World *World =c->getWorld();
    t_gamemodes gm;
    World->ReadGameMode(gm);
    if(gm.g_mode != GAMEMODE_DWARF)
    {
        con << "Only in fortress mode." << std::endl;
        c->Resume();
        return CR_FAILURE;
    }
    Maps = c->getMaps();
    if(!Maps->Start())
    {
        con << "Can't init map." << std::endl;
        c->Resume();
        return CR_FAILURE;
    }

    // Sanity check: map size
    uint32_t x_max_b, y_max_b, z_max_b;
    Maps->getSize(x_max_b,y_max_b,z_max_b);
    if(x_max != x_max_b || y_max != y_max_b || z_max != z_max_b)
    {
        con << "The map is not of the same size..." << std::endl;
        c->Resume();
        return CR_FAILURE;
    }

    // FIXME: add more sanity checks / MAP ID

    for(size_t i = 0; i < hidesaved.size();i++)
    {
        hideblock & hb = hidesaved[i];
        df_block * b = Maps->getBlock(hb.x,hb.y,hb.z);
        for (uint32_t i = 0; i < 16;i++) for (uint32_t j = 0; j < 16;j++)
        {
            b->designation[i][j].bits.hidden = hb.hiddens[i][j];
        }
    }
    // give back memory.
    hidesaved.clear();
    revealed = NOT_REVEALED;
    con << "Map hidden!" << std::endl;
    c->Resume();
    return CR_OK;
}

DFhackCExport command_result revtoggle (DFHack::Core * c, std::vector<std::string> & params)
{
    if(revealed)
    {
        return unreveal(c,params);
    }
    else
    {
        return reveal(c,params);
    }
}
