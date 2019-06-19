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

#ifndef SPDK_NVMF_POLL_GROUP_H
#define SPDK_NVMF_POLL_GROUP_H

struct spdk_nvmf_poll_group {
	struct spdk_thread				*thread;
	struct spdk_poller				*poller;

	TAILQ_HEAD(, spdk_nvmf_transport_poll_group)	tgroups;

	/* Array of poll groups indexed by subsystem id (sid) */
	struct spdk_nvmf_subsystem_poll_group		*sgroups;
	uint32_t					num_sgroups;

	/* All of the queue pairs that belong to this poll group */
	TAILQ_HEAD(, spdk_nvmf_qpair)			qpairs;
	TAILQ_ENTRY(spdk_nvmf_poll_group)			link;
};

/**
 * Create a poll group as an IO channel
 *
 * \param an io_device IO device (nvmf_tgt)
 * \param ctx_buf a pointer to spdk_nvmf_poll_group
 */
int spdk_nvmf_tgt_create_poll_group(void *io_device, void *ctx_buf);

/**
 * Destroy a poll group as an IO channel
 *
 * \param an io_device IO device (nvmf_tgt)
 * \param ctx_buf a pointer to spdk_nvmf_poll_group
 */
void spdk_nvmf_tgt_destroy_poll_group(void *io_device, void *ctx_buf);

#endif /* SPDK_NVMF_POLL_GROUP_H */
