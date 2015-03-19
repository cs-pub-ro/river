#include "CodeGen.h"

#include "common.h"
#include "cb.h"
#include "mm.h"
#include "modrm32.h"
#include "translatetbl.h"
#include "crc32.h"
#include "extern.h"

#include "river.h"

/* x86toriver converts a single x86 intruction to one ore more river instructions */
/* returns the instruction length */
/* dwInstrCount contains the number of generated river instructions */
DWORD x86toriver(RiverCodeGen *pEnv, BYTE *px86, RiverInstruction *pRiver, DWORD *dwInstrCount);

/* rivertox86 converts a block of river instructions to x86 */
/* returns the nuber of bytes written in px86 */
DWORD rivertox86(RiverCodeGen *pEnv, RiverRuntime *rt, RiverInstruction *pRiver, DWORD dwInstrCount, BYTE *px86, DWORD flags);

void TranslateReverse(RiverCodeGen *pEnv, RiverInstruction *rIn, RiverInstruction *rOut, DWORD *outCount);

RiverCodeGen::RiverCodeGen() {
	outBufferSize = 0;
	outBuffer = NULL;
	heap = NULL;
}

RiverCodeGen::~RiverCodeGen() {
	if (0 != outBufferSize) {
		Destroy();
	}
}

bool RiverCodeGen::Init(RiverHeap *hp, DWORD buffSz) {
	heap = hp;
	if (NULL == (outBuffer = (unsigned char *)EnvMemoryAlloc(buffSz))) {
		return false;
	}
	outBufferSize = buffSz;
	return true;
}

bool RiverCodeGen::Destroy() {
	EnvMemoryFree(outBuffer);
	outBuffer = NULL;
	outBufferSize = 0;
	heap = NULL;
	return true;
}

void RiverCodeGen::Reset() {
	addrCount = trInstCount = fwInstCount = bkInstCount = 0;
	memset(regVersions, 0, sizeof(regVersions));
}

struct RiverAddress *RiverCodeGen::AllocAddr() {
	struct RiverAddress *ret = &trRiverAddr[addrCount];
	addrCount++;
	return ret;
}

#define STRIP_REG_SIZE(reg) do { \
	if (RIVER_REG_NONE == (reg)) return reg; \
	reg &= 0x07; \
} while (0)

unsigned int RiverCodeGen::GetCurrentReg(unsigned char regName) const {
	STRIP_REG_SIZE(regName);
	return regVersions[regName] | regName;
}

unsigned int RiverCodeGen::GetPrevReg(unsigned char regName) const {
	STRIP_REG_SIZE(regName); 
	return (regVersions[regName] - 0x100) | regName;
}

unsigned int RiverCodeGen::NextReg(unsigned char regName) {
	regVersions[regName] += 0x100;
	return GetCurrentReg(regName);
}




DWORD dwTransLock = 0;

DWORD dwSysHandler    = (DWORD) SysHandler;
DWORD dwSysEndHandler = (DWORD) SysEndHandler;
DWORD dwBranchHandler = (DWORD) BranchHandler;

unsigned char *DuplicateBuffer(RiverHeap *h, unsigned char *p, unsigned int sz) {
	unsigned int mSz = (sz + 0x0F) & ~0x0F;
	
	unsigned char *pBuf = (unsigned char *)h->Alloc(mSz);
	if (pBuf == NULL) {
		return NULL;
	}

	memcpy(pBuf, p, sz);
	memset(pBuf + sz, 0x90, mSz - sz);
	return pBuf;
}

unsigned char *ConsolidateBlock(RiverHeap *h, unsigned char *outBuff, unsigned int outSz, unsigned char *saveBuff, unsigned int saveSz) {
	unsigned int mSz = (outSz + saveSz + 0x0F) & (~0x0F);

	unsigned char *pBuf = (unsigned char *)h->Alloc(mSz);
	if (NULL == pBuf) {
		return NULL;
	}

	memcpy(pBuf, saveBuff, saveSz);
	memcpy(&pBuf[saveSz], outBuff, outSz);
	memset(&pBuf[saveSz + outSz], 0x90, mSz - outSz - saveSz);
	return pBuf;
}

void MakeJMP(struct RiverInstruction *ri, DWORD jmpAddr) {
	ri->modifiers = 0;
	ri->specifiers = 0;
	ri->opCode = 0xE9;
	ri->opTypes[0] = RIVER_OPTYPE_IMM | RIVER_OPSIZE_32;
	ri->operands[0].asImm32 = jmpAddr;

	ri->opTypes[1] = RIVER_OPTYPE_IMM | RIVER_OPSIZE_32;
	ri->operands[1].asImm32 = 0;
}

bool RiverCodeGen::Translate(RiverBasicBlock *pCB, RiverRuntime *rt, DWORD dwTranslationFlags) {
	DWORD tmp;

	if (dwTranslationFlags & 0x80000000) {
		pCB->dwSize = 0;
		pCB->dwCRC = (DWORD)crc32(0xEDB88320, (BYTE *)pCB->address, 0);

		outBufferSize = 0;
		pCB->pCode = pCB->pFwCode = (unsigned char *)pCB->address;
		pCB->pBkCode = NULL;

		return true;
	} else {

		Reset();

		pCB->dwSize = x86toriver(this, (BYTE *)pCB->address, trRiverInst, &trInstCount);
		pCB->dwCRC = (DWORD)crc32(0xEDB88320, (BYTE *)pCB->address, pCB->dwSize);

		for (DWORD i = 0; i < fwInstCount; ++i) {
			TranslateReverse(this, &fwRiverInst[fwInstCount - 1 - i], &bkRiverInst[i], &tmp);
		}
		MakeJMP(&bkRiverInst[fwInstCount], pCB->address);

		bkInstCount = fwInstCount + 1;

		outBufferSize = rivertox86(this, rt, trRiverInst, trInstCount, outBuffer, 0x00);
		pCB->pCode = DuplicateBuffer(heap, outBuffer, outBufferSize);

		outBufferSize = rivertox86(this, rt, fwRiverInst, fwInstCount, outBuffer, 0x01);
		pCB->pFwCode = DuplicateBuffer(heap, outBuffer, outBufferSize);
		
		outBufferSize = rivertox86(this, rt, bkRiverInst, bkInstCount, outBuffer, 0x00);
		pCB->pBkCode = DuplicateBuffer(heap, outBuffer, outBufferSize);


		return true;
	}
}
