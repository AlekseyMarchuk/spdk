/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Mellanox Corporation. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "nvmf_internal.h"
#include "transport.h"
#include "spdk_internal/log.h"
#include "spdk/nvmf.h"

static int spdk_nvmf_poll_group_poll(void *ctx);

int
spdk_nvmf_poll_group_create(struct spdk_nvmf_tgt *tgt)
{
	struct spdk_io_channel *ch;
	struct spdk_nvmf_poll_group *pg;

	ch = spdk_get_io_channel(tgt);
	if (!ch) {
		SPDK_ERRLOG("Unable to get I/O channel for target\n");
		return -1;
	}

	pg = spdk_io_channel_get_ctx(ch);
	TAILQ_INSERT_TAIL(&tgt->poll_groups, pg, link);

	if (tgt->conn_sched.next_poll_group == NULL) {
		tgt->conn_sched.next_poll_group = pg;
	}

	return 0;
}

static void
spdk_nvmf_poll_group_destroy_thread(void *ctx)
{
	struct spdk_nvmf_poll_group *pg, *tpg;
	struct spdk_thread *thread = spdk_get_thread();
	struct spdk_nvmf_tgt *tgt = ctx;

	TAILQ_FOREACH_SAFE(pg, &tgt->poll_groups, link, tpg) {
		if (pg->thread == thread) {
			TAILQ_REMOVE(&tgt->poll_groups, pg, link);
			spdk_poller_unregister(&pg->poller);
			nvmf_tgt_disconnect_next_qpair(pg);
			return;
		}
	}
}

void spdk_nvmf_poll_groups_destroy(struct spdk_nvmf_tgt *tgt, spdk_msg_fn cpl)
{
	spdk_for_each_thread(spdk_nvmf_poll_group_destroy_thread, tgt, cpl);
}

int
spdk_nvmf_tgt_create_poll_group(void *io_device, void *ctx_buf)
{
	struct spdk_nvmf_tgt *tgt = io_device;
	struct spdk_nvmf_poll_group *group = ctx_buf;
	struct spdk_nvmf_transport *transport;
	uint32_t sid;

	TAILQ_INIT(&group->tgroups);
	TAILQ_INIT(&group->qpairs);

	TAILQ_FOREACH(transport, &tgt->transports, link) {
		spdk_nvmf_poll_group_add_transport(group, transport);
	}

	group->num_sgroups = tgt->max_subsystems;
	group->sgroups = calloc(tgt->max_subsystems, sizeof(struct spdk_nvmf_subsystem_poll_group));
	if (!group->sgroups) {
		return -ENOMEM;
	}

	for (sid = 0; sid < tgt->max_subsystems; sid++) {
		struct spdk_nvmf_subsystem *subsystem;

		subsystem = tgt->subsystems[sid];
		if (!subsystem) {
			continue;
		}

		if (spdk_nvmf_poll_group_add_subsystem(group, subsystem, NULL, NULL) != 0) {
			spdk_nvmf_tgt_destroy_poll_group(io_device, ctx_buf);
			return -1;
		}
	}

	group->poller = spdk_poller_register(spdk_nvmf_poll_group_poll, group, 0);
	group->thread = spdk_get_thread();

	return 0;
}

void
spdk_nvmf_tgt_destroy_poll_group(void *io_device, void *ctx_buf)
{
	struct spdk_nvmf_poll_group *group = ctx_buf;
	struct spdk_nvmf_transport_poll_group *tgroup, *tmp;
	struct spdk_nvmf_subsystem_poll_group *sgroup;
	uint32_t sid, nsid;

	TAILQ_FOREACH_SAFE(tgroup, &group->tgroups, link, tmp) {
		TAILQ_REMOVE(&group->tgroups, tgroup, link);
		spdk_nvmf_transport_poll_group_destroy(tgroup);
	}

	for (sid = 0; sid < group->num_sgroups; sid++) {
		sgroup = &group->sgroups[sid];

		for (nsid = 0; nsid < sgroup->num_ns; nsid++) {
			if (sgroup->ns_info[nsid].channel) {
				spdk_put_io_channel(sgroup->ns_info[nsid].channel);
				sgroup->ns_info[nsid].channel = NULL;
			}
		}

		free(sgroup->ns_info);
	}

	free(group->sgroups);
}

static void
_spdk_nvmf_poll_group_add(void *ctx)
{
	struct spdk_nvmf_qpair *qpair = ctx;
	struct spdk_nvmf_poll_group *group = qpair->group;
	struct spdk_nvmf_transport_poll_group *tgroup;
	int rc;

	TAILQ_FOREACH(tgroup, &group->tgroups, link) {
		if (tgroup->transport == qpair->transport) {
			rc = spdk_nvmf_transport_poll_group_add(tgroup, qpair);
			break;
		}
	}

	/* We add the qpair to the group only it is succesfully added into the tgroup */
	if (rc == 0) {
		TAILQ_INSERT_TAIL(&group->qpairs, qpair, link);
		spdk_nvmf_qpair_set_state(qpair, SPDK_NVMF_QPAIR_ACTIVE);
	}
}

int spdk_nvmf_poll_group_add(struct spdk_nvmf_tgt *tgt, struct spdk_nvmf_qpair *qpair)
{
	struct spdk_nvmf_poll_group *pg = spdk_nvmf_tgt_poll_group_select(tgt, qpair);
	if (!pg) {
		return -1;
	}

	qpair->group = pg;

	TAILQ_INIT(&qpair->outstanding);

	spdk_thread_send_msg(qpair->group->thread, _spdk_nvmf_poll_group_add, qpair);

	return 0;
}

int
spdk_nvmf_poll_group_add_transport(struct spdk_nvmf_poll_group *group,
				   struct spdk_nvmf_transport *transport)
{
	struct spdk_nvmf_transport_poll_group *tgroup;

	TAILQ_FOREACH(tgroup, &group->tgroups, link) {
		if (tgroup->transport == transport) {
			/* Transport already in the poll group */
			return 0;
		}
	}

	tgroup = spdk_nvmf_transport_poll_group_create(transport);
	if (!tgroup) {
		SPDK_ERRLOG("Unable to create poll group for transport\n");
		return -1;
	}

	TAILQ_INSERT_TAIL(&group->tgroups, tgroup, link);

	return 0;
}

static int
spdk_nvmf_poll_group_poll(void *ctx)
{
	struct spdk_nvmf_poll_group *group = ctx;
	int rc;
	int count = 0;
	struct spdk_nvmf_transport_poll_group *tgroup;

	TAILQ_FOREACH(tgroup, &group->tgroups, link) {
		rc = spdk_nvmf_transport_poll_group_poll(tgroup);
		if (rc < 0) {
			return -1;
		}
		count += rc;
	}

	return count;
}
