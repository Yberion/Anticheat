#include "server.h"

// Update value of packets and FPS
// From JK2MV (updated version by fau)
static void EslAnticheat_CalcPacketsFPS(client_t *client, int *packets, int *fps)
{
	int		lastCmdTime;
	int		lastPacketIndex = 0;
	int		i;

	lastCmdTime = client->eslAnticheat.cmdStats[client->eslAnticheat.cmdIndex & (CMD_MASK - 1)].serverTime;

	for (i = 0; i < CMD_MASK; i++)
	{
		ucmdStat_t *stat = &client->eslAnticheat.cmdStats[i];

		if (stat->serverTime + 1000 >= lastCmdTime)
		{
			(*fps)++;

			if (lastPacketIndex != stat->packetIndex)
			{
				lastPacketIndex = stat->packetIndex;
				(*packets)++;
			}
		}
	}
}

/*
===================
EslAnticheat_CalcTimeNudges
Updates cl->eslAnticheat.timenudge variables
===================
*/
/*
void EslAnticheat_CalcTimeNudges(void)
{
    int			i;
    client_t*   cl;

    for (i = 0; i < sv_maxclients->integer; i++)
    {
        cl = &svs.clients[i];

        if (cl->state != CS_ACTIVE)
        {
            cl->eslAnticheat.timeNudge2 = 0;

            continue;
        }

        if (cl->gentity->r.svFlags & SVF_BOT)
        {
            cl->eslAnticheat.timeNudge2 = 0;

            continue;
        }

        cl->eslAnticheat.delayCount2++;
        cl->eslAnticheat.delaySum2 += cl->lastUsercmd.serverTime - sv.time;
        cl->eslAnticheat.pingSum2 += cl->ping;

        if (svs.time > cl->eslAnticheat.lastTimetimeNudgeCalculation2 + 1000)
        {
            cl->eslAnticheat.timeNudge2 = ((cl->eslAnticheat.delaySum2 / (float)cl->eslAnticheat.delayCount2) + (cl->eslAnticheat.pingSum2 / (float)cl->eslAnticheat.delayCount2) + 11 + (1000 / (float)sv_fps->integer)) * -1;

            cl->eslAnticheat.delayCount2 = 0;
            cl->eslAnticheat.delaySum2 = 0;
            cl->eslAnticheat.pingSum2 = 0;

            cl->eslAnticheat.lastTimetimeNudgeCalculation2 = svs.time;
        }
    }
}
*/

/*
==================
EslAnticheat_NetStatus_f

Display net settings of all players
==================
*/
void EslAnticheat_NetStatus_f(client_t* client)
{
	if (client->eslAnticheat.lastTimeNetStatus + 500 > svs.time)
	{
		return;
	}

	char			status[4096];
	int				i;
	client_t		*cl;
	playerState_t	*ps;
	int				ping;
	char			state[32];

	status[0] = 0;

	//Q_strcat(status, sizeof(status), "cl score ping rate  fps packets timeNudge timeNudge2 name \n");
	Q_strcat(status, sizeof(status), "cl score ping rate   fps packets timeNudge snaps name \n");
	Q_strcat(status, sizeof(status), "-- ----- ---- ------ --- ------- --------- ----- ---------------\n");

	for (i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++)
	{
		int			fps = 0;
		int			packets = 0;

		if (!cl->state)
			continue;

		if (cl->state == CS_CONNECTED)
			Q_strncpyz(state, "CON ", sizeof(state));
		else if (cl->state == CS_ZOMBIE)
			Q_strncpyz(state, "ZMB ", sizeof(state));
		else {
			ping = cl->ping < 9999 ? cl->ping : 9999;
			Com_sprintf(state, sizeof(state), "%4i", ping);
		}

		ps = SV_GameClientNum(i);

		// If not a bot
		if (cl->ping >= 1)
		{
			EslAnticheat_CalcPacketsFPS(cl, &packets, &fps);
		}

		// No need for truncation "feature" if we move name to end
		//Q_strcat(status, sizeof(status), va("%2i %5i %s %5i %3i %7i %9i %10i %s^7\n", i, ps->persistant[PERS_SCORE], state, cl->rate, fps, packets, cl->timeNudge, cl->timeNudge2, cl->name));
		Q_strcat(status, sizeof(status), va("%2i %5i %s %6i %3i %7i %9i %5i %s^7\n", i, ps->persistant[PERS_SCORE], state, cl->rate, fps, packets, cl->eslAnticheat.timeNudge, cl->wishSnaps, cl->name));
	}

	client->eslAnticheat.lastTimeNetStatus = svs.time;

	char buffer[1012] = { 0 };
	int statusLength = strlen(status);
	int currentProgress = 0;

	while (currentProgress < statusLength)
	{
		Q_strncpyz(buffer, &status[currentProgress], sizeof(buffer));

		if (strlen(buffer) < 1012)
		{
			Q_strcat(buffer, sizeof(buffer), "\n");
		}

		SV_SendServerCommand(client, "print \"%s", buffer);
		currentProgress += strlen(buffer);
	}
}

static void EslAnticheat_UpdatePacketsAndFPS(client_t* cl, usercmd_t* cmd, int packetIndex)
{
	// TODO
	// Clean le buffer après avoir changer de map
	// Reset le timer des 10 seconds par la même occasion ?
	cl->eslAnticheat.cmdIndex++;
	cl->eslAnticheat.cmdStats[cl->eslAnticheat.cmdIndex & (CMD_MASK - 1)].serverTime = cmd->serverTime;
	cl->eslAnticheat.cmdStats[cl->eslAnticheat.cmdIndex & (CMD_MASK - 1)].packetIndex = packetIndex;
}

static void EslAnticheat_UpdatePingAndDelay(client_t* cl, usercmd_t* cmd)
{
	cl->eslAnticheat.delayCount++;
	cl->eslAnticheat.delaySum += cmd->serverTime - sv.time;
	cl->eslAnticheat.pingSum += cl->ping;
}

static void EslAnticheat_CalcTimenudge(client_t* cl, int Sys_Milliseconds_)
{
	// Wait 1000 ms to have enough data to calcul an approximation of the timenudge
	if (Sys_Milliseconds_ < cl->eslAnticheat.lastTimeTimeNudgeCalculation + 1000)
	{
		return;
	}

	// ((serverTime - sv.time) + ping -18 + (1000/sv_fps)) * -1
	cl->eslAnticheat.timeNudge = ((cl->eslAnticheat.delaySum / (float)cl->eslAnticheat.delayCount) + (cl->eslAnticheat.pingSum / (float)cl->eslAnticheat.delayCount) - 18 + (1000 / (float)sv_fps->integer)) * -1;

	cl->eslAnticheat.delayCount = 0;
	cl->eslAnticheat.delaySum = 0;
	cl->eslAnticheat.pingSum = 0;

	cl->eslAnticheat.lastTimeTimeNudgeCalculation = Sys_Milliseconds_;
}

static void EslAnticheat_UpdateAveragePingSinceConnected(client_t* cl, int Sys_Milliseconds_)
{
	// Only start to update after 10 seconds being on the server to have a stabilized the ping
	if (svs.time - cl->lastConnectTime <= 10000)
	{
		return;
	}

	// There is probably a problem or something else (vid_restart), don't update the average ping
	if (cl->ping >= 900)
	{
		return;
	}

	// Update every 500ms
	if (Sys_Milliseconds_ <= cl->eslAnticheat.lastTimetAvgPingCalculation + 500)
	{
		return;
	}

	float averagePingAtThisTime = (cl->eslAnticheat.pingSum / (float)cl->eslAnticheat.delayCount);

	cl->eslAnticheat.averagePingCount++;

	if (cl->eslAnticheat.averagePingSinceConnected == 0)
	{
		cl->eslAnticheat.averagePingSinceConnected = averagePingAtThisTime;
	}
	else
	{
		cl->eslAnticheat.averagePingSinceConnected += averagePingAtThisTime;
	}

	//SV_SendServerCommand(NULL, "print \"%s^7 have %f avg ping since connected\n", cl->name, cl->eslAnticheat.averagePingSinceConnected / cl->eslAnticheat.averagePingCount);

	Com_Printf("%s^7 have %f avg ping since connected\n", cl->name, cl->eslAnticheat.averagePingSinceConnected / cl->eslAnticheat.averagePingCount);

	cl->eslAnticheat.lastTimetAvgPingCalculation = Sys_Milliseconds_;
}

static void EslAnticheat_MonitorPackets(client_t* cl, int Sys_Milliseconds_)
{
	// Only check every 1000ms
	if (Sys_Milliseconds_ <= cl->eslAnticheat.lastTimetPacketsFPSCalculation + 1000)
	{
		return;
	}

	/*
	// Alternative methode
	clientSnapshot_t* frame;

	frame = &cl->frames[cl->netchan.outgoingSequence & PACKET_MASK];

	sharedEntity_t *ent = SV_GentityNum(frame->ps.clientNum);

	if (ent->playerState->pm_type != PM_SPECTATOR && !(ent->playerState->pm_flags & PMF_FOLLOW) && !(ent->r.svFlags & SVF_BOT))
	*/

	int clientNum = cl - svs.clients;

	playerState_t* ps = SV_GameClientNum(clientNum);
	sharedEntity_t* ent = SV_GentityNum(clientNum);

	// This mean the player is actually in game and not a bot
	if (ps->pm_type != PM_SPECTATOR && !(ps->pm_flags & PMF_FOLLOW) && !(ent->r.svFlags & SVF_BOT))
	{
		int			fps = 0;
		int			packets = 0;

		EslAnticheat_CalcPacketsFPS(cl, &packets, &fps);

		SV_SendServerCommand(cl, "print \"%i %i\n", fps, packets);

		if (cl->eslAnticheat.inGameTime > sv_eslAnticheat_packetsIngameDelayBeforeWarnings->integer * 1000)
		{
			if (packets < sv_eslAnticheat_packetsMinimumAllowed->integer - sv_eslAnticheat_packetsErrorMargin->integer || packets > sv_eslAnticheat_packetsMaximumAllowed->integer + sv_eslAnticheat_packetsErrorMargin->integer)
			{
				if (Sys_Milliseconds_ > cl->eslAnticheat.lastTimePacketsWarning + (sv_eslAnticheat_packetsIngameDelayBetweenWarnings->integer * 1000))
				{
					SV_SendServerCommand(NULL, "chat \"^1Warning: ^7%s ^1is sending ^3%i ^1packets per frame to the server^7\n", cl->name, packets);
					cl->eslAnticheat.lastTimePacketsWarning = Sys_Milliseconds_;
				}
			}
		}

		cl->eslAnticheat.inGameTime += 1000;
	}
	else
	{
		cl->eslAnticheat.inGameTime = 0;
		cl->eslAnticheat.lastTimetPacketsFPSCalculation = 0;
	}

	cl->eslAnticheat.lastTimetPacketsFPSCalculation = Sys_Milliseconds_;
}

void EslAnticheat_main(client_t* cl, usercmd_t* cmd, int packetIndex, int Sys_Milliseconds_)
{
	// If a bot
	if (cl->ping < 1)
	{
		return;
	}

	EslAnticheat_UpdatePacketsAndFPS(cl, cmd, packetIndex);

	EslAnticheat_UpdatePingAndDelay(cl, cmd);

	EslAnticheat_CalcTimenudge(cl, Sys_Milliseconds_);

	//EslAnticheat_UpdateAveragePingSinceConnected(cl, Sys_Milliseconds_);

	//EslAnticheat_MonitorPackets(cl, Sys_Milliseconds_);
}