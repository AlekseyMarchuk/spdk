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
#include "spdk_internal/log.h"
#include "spdk/nvmf.h"


/* Round robin selection of poll groups */
static struct spdk_nvmf_poll_group *
spdk_nvmf_get_next_pg(struct spdk_nvmf_tgt *tgt)
{
	struct spdk_nvmf_poll_group *pg;

	pg = tgt->conn_sched.next_poll_group;
	tgt->conn_sched.next_poll_group = TAILQ_NEXT(pg, link);
	if (tgt->conn_sched.next_poll_group == NULL) {
		tgt->conn_sched.next_poll_group = TAILQ_FIRST(&tgt->poll_groups);
	}

	return pg;
}

struct spdk_nvmf_poll_group *
spdk_nvmf_tgt_poll_group_select(struct spdk_nvmf_tgt *tgt, struct spdk_nvmf_qpair *qpair)
{
	struct spdk_nvme_transport_id trid;
	struct spdk_nvmf_tgt_host_trid *tmp_trid = NULL, *new_trid = NULL;
	struct spdk_nvmf_poll_group *pg = NULL;
	int ret;

	switch (tgt->conf->conn_sched) {
	case CONNECT_SCHED_HOST_IP:
		ret = spdk_nvmf_qpair_get_peer_trid(qpair, &trid);
		if (ret) {
			pg = spdk_nvmf_get_next_pg(tgt);
			SPDK_ERRLOG("Invalid host transport Id. Assigning to poll group %p\n", pg);
			break;
		}

		TAILQ_FOREACH(tmp_trid, &tgt->conn_sched.host_trids, link) {
			if (tmp_trid && !strncmp(tmp_trid->host_trid.traddr,
						 trid.traddr, SPDK_NVMF_TRADDR_MAX_LEN + 1)) {
				tmp_trid->ref++;
				pg = tmp_trid->pg;
				break;
			}
		}
		if (!tmp_trid) {
			new_trid = calloc(1, sizeof(*new_trid));
			if (!new_trid) {
				pg = spdk_nvmf_get_next_pg(tgt);
				SPDK_ERRLOG("Insufficient memory. Assigning to poll group %p\n", pg);
				break;
			}
			/* Get the next available poll group for the new host */
			pg = spdk_nvmf_get_next_pg(tgt);
			new_trid->pg = pg;
			memcpy(new_trid->host_trid.traddr, trid.traddr,
			       SPDK_NVMF_TRADDR_MAX_LEN + 1);
			TAILQ_INSERT_TAIL(&tgt->conn_sched.host_trids, new_trid, link);
		}
		break;
	case CONNECT_SCHED_ROUND_ROBIN:
	default:
		pg = spdk_nvmf_get_next_pg(tgt);
		break;
	}

	return pg;
}
