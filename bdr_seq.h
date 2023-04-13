#ifndef BDR_SEQ_H
#define BDR_SEQ_H

struct CreateSeqStmt;
struct AlterSeqStmt;

#ifdef HAVE_SEQAM

extern Oid	BdrVotesRelid;
extern Oid	BdrSeqamOid;
extern Oid	BdrSequenceValuesRelid;
extern Oid	BdrSequenceElectionsRelid;

static inline bool
isBdrGlobalSeqRelId(Oid relid)
{
	return relid == BdrSequenceValuesRelid ||
		relid == BdrSequenceElectionsRelid ||
		relid == BdrVotesRelid;
}

extern void bdr_sequencer_set_nnodes(int nnodes);

extern void bdr_sequencer_shmem_init(int sequencers);
extern void bdr_sequencer_init(int seq_slot, int nnodes);
extern void bdr_sequencer_lock(void);
extern bool bdr_sequencer_vote(void);
extern void bdr_sequencer_tally(void);
extern bool bdr_sequencer_start_elections(void);
extern void bdr_sequencer_fill_sequences(void);

extern void bdr_sequencer_wakeup(void);
extern void bdr_schedule_eoxact_sequencer_wakeup(void);

extern int	bdr_sequencer_get_next_free_slot(void);

extern void filter_CreateBdrSeqStmt(struct CreateSeqStmt *stmt);
extern void filter_AlterBdrSeqStmt(struct AlterSeqStmt *stmt, Oid seqoid);

extern void bdr_maintain_seq_schema(Oid schema_oid);

#else

#define GLOBAL_SEQ_ERROR() global_seq_error(__func__, __LINE__)

inline static void
global_seq_error(const char *func, int line)
{
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("BDR global sequences not supported on this install (in %s:%d)",
					func, line)));
};

/* Do-nothing stubs for when global sequences disabled */
static inline bool
isBdrGlobalSeqRelId(Oid relid)
{
	return false;
}

inline static void
bdr_sequencer_set_nnodes(Size nnodes)
{
};

inline static void
bdr_sequencer_shmem_init(int sequencers)
{
};
inline static void
bdr_sequencer_init(int seq_slot, Size nnodes)
{
};
inline static void
bdr_sequencer_lock(void)
{
};
inline static bool
bdr_sequencer_vote(void)
{
	return false;
};
inline static void
bdr_sequencer_tally(void)
{
};
inline static bool
bdr_sequencer_start_elections(void)
{
	return false;
};
inline static void
bdr_sequencer_fill_sequences(void)
{
};

inline static void
bdr_sequencer_wakeup(void)
{
};
inline static void
bdr_schedule_eoxact_sequencer_wakeup(void)
{
};

inline static int
bdr_sequencer_get_next_free_slot(void)
{
	return -1;
};

inline static void
filter_CreateBdrSeqStmt(struct CreateSeqStmt *stmt)
{
};
inline static void
filter_AlterBdrSeqStmt(struct AlterSeqStmt *stmt, Oid seqoid)
{
};

inline static void
bdr_maintain_seq_schema(Oid schema_oid)
{
};

#endif							/* HAVE_SEQAM */

#endif
