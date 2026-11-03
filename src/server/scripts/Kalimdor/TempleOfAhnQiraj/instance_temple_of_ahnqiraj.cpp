/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "CreatureGroups.h"
#include "GameTime.h"
#include "InstanceMapScript.h"
#include "InstanceScript.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "TaskScheduler.h"
#include "temple_of_ahnqiraj.h"

ObjectData const creatureData[] =
{
    { NPC_VEM,          DATA_VEM          },
    { NPC_KRI,          DATA_KRI          },
    { NPC_YAUJ,         DATA_YAUJ         },
    { NPC_SARTURA,      DATA_SARTURA      },
    { NPC_CTHUN,        DATA_CTHUN        },
    { NPC_EYE_OF_CTHUN, DATA_EYE_OF_CTHUN },
    { NPC_OURO,         DATA_OURO         },
    { NPC_OURO_SPAWNER, DATA_OURO_SPAWNER },
    { NPC_MASTERS_EYE,  DATA_MASTERS_EYE  },
    { NPC_VEKLOR,       DATA_VEKLOR       },
    { NPC_VEKNILASH,    DATA_VEKNILASH    },
    { NPC_VISCIDUS,     DATA_VISCIDUS     },
    { 0,                0                 }
};

DoorData const doorData[] =
{
    { AQ40_DOOR_SKERAM,      DATA_SKERAM,        DOOR_TYPE_PASSAGE },
    { AQ40_DOOR_TE_ENTRANCE, DATA_HUHURAN,       DOOR_TYPE_PASSAGE },
    { AQ40_DOOR_TE_ENTRANCE, DATA_TWIN_EMPERORS, DOOR_TYPE_ROOM    },
    { AQ40_DOOR_TE_EXIT,     DATA_TWIN_EMPERORS, DOOR_TYPE_PASSAGE },
    { 0,                     0,                  DOOR_TYPE_ROOM}
};

class instance_temple_of_ahnqiraj : public InstanceMapScript
{
public:
    instance_temple_of_ahnqiraj() : InstanceMapScript("instance_temple_of_ahnqiraj", MAP_AHN_QIRAJ_TEMPLE) { }

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_temple_of_ahnqiraj_InstanceMapScript(map);
    }

    struct instance_temple_of_ahnqiraj_InstanceMapScript : public InstanceScript
    {
        instance_temple_of_ahnqiraj_InstanceMapScript(Map* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(MAX_BOSS_NUMBER);
            LoadObjectData(creatureData, nullptr);
            LoadDoorData(doorData);
        }

        void Initialize() override
        {
            BugTrioDeathCount = 0;
        }

        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_OURO_SPAWNER:
                    if (GetBossState(DATA_OURO) != DONE)
                        creature->Respawn();
                    break;
                case NPC_MASTERS_EYE:
                    if (GetBossState(DATA_TWIN_EMPERORS) != DONE && !creature->IsAlive())
                        creature->Respawn(true);
                    break;
                case NPC_CTHUN:
                    if (!creature->IsAlive())
                    {
                        for (ObjectGuid const& guid : CThunGraspGUIDs)
                        {
                            if (GameObject* cthunGrasp = instance->GetGameObject(guid))
                            {
                                cthunGrasp->DespawnOrUnsummon(1s);
                            }
                        }
                    }
                    break;
                default:
                    break;
            }

            InstanceScript::OnCreatureCreate(creature);
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_CTHUN_GRASP:
                    CThunGraspGUIDs.push_back(go->GetGUID());
                    if (Creature* CThun = GetCreature(DATA_CTHUN))
                    {
                        if (!CThun->IsAlive())
                        {
                            go->DespawnOrUnsummon(1s);
                        }
                    }
                    break;
                default:
                    break;
            }

            InstanceScript::OnGameObjectCreate(go);
        }

        void OnUnitDeath(Unit* unit) override
        {
            switch (unit->GetEntry())
            {
                case NPC_QIRAJI_SLAYER:
                case NPC_QIRAJI_MINDSLAYER:
                    if (Creature* creature = unit->ToCreature())
                    {
                        if (CreatureGroup* formation = creature->GetFormation())
                        {
                            scheduler.Schedule(100ms, [formation](TaskContext /*context*/)
                            {
                                if (!formation->IsAnyMemberAlive(true))
                                {
                                    if (Creature* leader = formation->GetLeader())
                                    {
                                        if (leader->IsAlive())
                                        {
                                            leader->AI()->SetData(0, 1);
                                        }
                                    }
                                }
                            });
                        }
                    }
                    break;
                case NPC_CTHUN:
                    for (ObjectGuid const& guid : CThunGraspGUIDs)
                    {
                        if (GameObject* cthunGrasp = instance->GetGameObject(guid))
                        {
                            cthunGrasp->DespawnOrUnsummon(1s);
                        }
                    }
                    break;
                default:
                    break;
            }
        }

        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_BUG_TRIO_DEATH:
                    return BugTrioDeathCount;
            }
            return 0;
        }

        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case DATA_BUG_TRIO_DEATH:
                    if (data != 0)
                        ++BugTrioDeathCount;
                    else
                        BugTrioDeathCount = 0;
                    break;
                default:
                    break;
            }
        }

        bool CheckRequiredBosses(uint32 bossId, Player const* player) const override
        {
            if (_SkipCheckRequiredBosses(player))
                return true;

            switch (bossId)
            {
                case DATA_TWIN_EMPERORS:
                    if (GetBossState(DATA_HUHURAN) != DONE)
                        return false;
                    break;
                case DATA_CTHUN:
                    if (GetBossState(DATA_TWIN_EMPERORS) != DONE)
                        return false;
                    break;
                default:
                    break;
            }

            return true;
        }

        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                return false;

            switch (type)
            {
                case DATA_OURO:
                    if (state == FAIL)
                        RespawnOuroSpawner();
                    break;
                default:
                    break;
            }

            return true;
        }

        void OnPlayerEnter(Player* player) override
        {
            InstanceScript::OnPlayerEnter(player);

            // XorWoW: also heals instances whose spawner already sits on its weekly timer after a wipe
            EncounterState ouroState = GetBossState(DATA_OURO);
            if (ouroState != DONE && ouroState != IN_PROGRESS && !GetCreature(DATA_OURO))
                RespawnOuroSpawner();
        }

        // XorWoW: the spawner despawns when it summons Ouro. Under dynamic respawn a despawned creature leaves the map,
        // so GetCreature() found nothing to Respawn() after a wipe and the spawner waited out its 7-day timer instead.
        // A respawn time of now lets the map's respawn tick (every 5 s) bring it back.
        void RespawnOuroSpawner()
        {
            if (Creature* ouroSpawner = GetCreature(DATA_OURO_SPAWNER))
            {
                if (!ouroSpawner->IsAlive())
                    ouroSpawner->Respawn();
                return;
            }

            std::vector<ObjectGuid::LowType> pending;
            for (auto const& [spawnId, respawnTime] : instance->GetCreatureRespawnTimes())
                if (CreatureData const* data = sObjectMgr->GetCreatureData(spawnId))
                    if (data->id == NPC_OURO_SPAWNER)
                        pending.push_back(spawnId);

            time_t now = GameTime::GetGameTime().count();
            for (ObjectGuid::LowType spawnId : pending)
                instance->SaveCreatureRespawnTime(spawnId, now);
        }

    private:
        GuidVector CThunGraspGUIDs;
        uint32 BugTrioDeathCount;
    };
};

void AddSC_instance_temple_of_ahnqiraj()
{
    new instance_temple_of_ahnqiraj();
}
