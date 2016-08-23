#include "RevSymbolicEnvironment.h"

#include "../revtracer/execenv.h"
#include "../revtracer/Tracking.h"

static rev::BYTE GetFundamentalRegister(rev::BYTE reg) {
	if (reg < 0x20) {
		return reg & 0x07;
	}
	return reg;
}

RevSymbolicEnvironment::RevSymbolicEnvironment(void *revEnv) {
	pEnv = revEnv;
}

void RevSymbolicEnvironment::GetOperandLayout(const RiverInstruction &rIn) {
	unsigned int trackIndex = 0;

	for (unsigned int i = 0; i < 4; ++i) {
		addressOffsets[i] = 0xFFFFFFFF;
	}

	for (unsigned int i = 0; i < 4; ++i) {
		valueOffsets[i] = 0xFFFFFFFF;
	}

	flagOffset = 0xFFFFFFFF;

	if ((0 == (RIVER_SPEC_IGNORES_FLG & rIn.specifiers)) || (rIn.modFlags)) {
		flagOffset = trackIndex;
		trackIndex++;
	}

	for (int i = 3; i >= 0; --i) {
		if ((RIVER_OPTYPE(rIn.opTypes[i]) == RIVER_OPTYPE_MEM) && (0 != rIn.operands[i].asAddress->type)) {
			addressOffsets[i] = trackIndex;
			trackIndex += rIn.operands[i].asAddress->HasSegment() ? 2 : 1;
		}
	}

	for (int i = 3; i >= 0; --i) {
		if (0 == (RIVER_SPEC_IGNORES_OP(i) & rIn.specifiers)) {
			switch (RIVER_OPTYPE(rIn.opTypes[i])) {
			case RIVER_OPTYPE_NONE:
			case RIVER_OPTYPE_IMM:
				break;
			case RIVER_OPTYPE_REG:
			case RIVER_OPTYPE_MEM:
				valueOffsets[i] = trackIndex;
				trackIndex++;
				break;
			default:
				_asm int 3;
			}
		}
	}
}

bool RevSymbolicEnvironment::SetCurrentInstruction(RiverInstruction *rIn, void *context) {
	opBase = (rev::DWORD *)context;
	current = rIn;

	GetOperandLayout(*current);
	return true;
}

void RevSymbolicEnvironment::PushState(stk::LargeStack &stack) { }
void RevSymbolicEnvironment::PopState(stk::LargeStack &stack) { }

bool RevSymbolicEnvironment::GetOperand(rev::BYTE opIdx, rev::BOOL &isTracked, rev::DWORD &concreteValue, void *&symbolicValue) {
	void *symExpr;

	if (RIVER_SPEC_IGNORES_OP(opIdx) & current->specifiers) {
		return false;
	}

	switch (RIVER_OPTYPE(current->opTypes[opIdx])) {
	case RIVER_OPTYPE_NONE:
		return false;

	case RIVER_OPTYPE_IMM:
		isTracked = false;
		switch (RIVER_OPSIZE(current->opTypes[opIdx])) {
		case RIVER_OPSIZE_32:
			concreteValue = current->operands[opIdx].asImm32;
			break;
		case RIVER_OPSIZE_16:
			concreteValue = current->operands[opIdx].asImm16;
			break;
		case RIVER_OPSIZE_8:
			concreteValue = current->operands[opIdx].asImm8;
			break;
		}
		return true;

	case RIVER_OPTYPE_REG:
		symExpr = (void *)((ExecutionEnvironment *)pEnv)->runtimeContext.taintedRegisters[GetFundamentalRegister(current->operands[opIdx].asRegister.name)];

		isTracked = (symExpr != NULL);
		symbolicValue = symExpr;
		concreteValue = opBase[-(valueOffsets[opIdx])];
		return true;

	case RIVER_OPTYPE_MEM:
		// how do we handle dereferenced symbolic values?

		if (0 == current->operands[opIdx].asAddress->type) {
			symExpr = (void *)((ExecutionEnvironment *)pEnv)->runtimeContext.taintedRegisters[GetFundamentalRegister(current->operands[opIdx].asAddress->base.name)];

			isTracked = (symExpr != NULL);
			symbolicValue = symExpr;
			concreteValue = opBase[-(valueOffsets[opIdx])];
			return true;
		}

		if (RIVER_SPEC_IGNORES_MEMORY & current->specifiers) {
			return false; // for now!
		}

		//symExpr = get from opBase[opOffset[opIdx]]

		if (opBase[-(addressOffsets[opIdx])] < 0x1000) {
			__asm int 3;
		}

		symExpr = (void *)TrackAddr(pEnv, opBase[-(addressOffsets[opIdx])], 0);
		isTracked = (symExpr != NULL);
		symbolicValue = symExpr;
		concreteValue = opBase[-(valueOffsets[opIdx])];
		return true;
	}

	return false;
}

unsigned int BinLog2(unsigned int v) {
	register unsigned int r; // result of log2(v) will go here
	register unsigned int shift;

	r = (v > 0xFFFF) << 4; v >>= r;
	shift = (v > 0xFF) << 3; v >>= shift; r |= shift;
	shift = (v > 0x0F) << 2; v >>= shift; r |= shift;
	shift = (v > 0x03) << 1; v >>= shift; r |= shift;
	r |= (v >> 1);

	return r;
}

unsigned int flagShifts[] = {
	0, //RIVER_SPEC_FLAG_CF
	2, //RIVER_SPEC_FLAG_PF
	4, //RIVER_SPEC_FLAG_AF
	6, //RIVER_SPEC_FLAG_ZF
	7, //RIVER_SPEC_FLAG_SF
	11, //RIVER_SPEC_FLAG_OF
	10 //RIVER_SPEC_FLAG_DF
};

bool RevSymbolicEnvironment::GetFlgValue(rev::BYTE flg, rev::BOOL &isTracked, rev::BYTE &concreteValue, void *&symbolicValue) {
	if (0 == (flg & current->testFlags)) {
		return false;
	}

	unsigned int flgIdx = BinLog2(flg);
	//void *expr = pEnv->runtimeContext.taintedFlags[flgIdx];

	rev::DWORD val = ((ExecutionEnvironment *)pEnv)->runtimeContext.taintedFlags[flgIdx];

	isTracked = (0 != val);
	concreteValue = (opBase[flagOffset] >> flagShifts[flgIdx]) & 1;
	if (isTracked) {
		symbolicValue = (void *)val;
	}

	return true;
}

bool RevSymbolicEnvironment::SetOperand(rev::BYTE opIdx, void *symbolicValue) {
	switch (RIVER_OPTYPE(current->opTypes[opIdx])) {
	case RIVER_OPTYPE_NONE:
	case RIVER_OPTYPE_IMM:
		return false;

	case RIVER_OPTYPE_REG:
		((ExecutionEnvironment *)pEnv)->runtimeContext.taintedRegisters[GetFundamentalRegister(current->operands[opIdx].asRegister.name)] = (rev::DWORD)symbolicValue;
		return true;

	case RIVER_OPTYPE_MEM:
		if (0 == current->operands[opIdx].asAddress->type) {
			((ExecutionEnvironment *)pEnv)->runtimeContext.taintedRegisters[GetFundamentalRegister(current->operands[opIdx].asAddress->base.name)] = (rev::DWORD)symbolicValue;
		}
		else {
			MarkAddr(pEnv, opBase[-(addressOffsets[opIdx])], (rev::DWORD)symbolicValue, 0);
		}
		return true;

	}

	return false;
}

bool RevSymbolicEnvironment::UnsetOperand(rev::BYTE opIdx) {
	return SetOperand(opIdx, nullptr);
}

void RevSymbolicEnvironment::SetFlgValue(rev::BYTE flg, void *symbolicValue) {
	unsigned int flgIdx = BinLog2(flg);

	((ExecutionEnvironment *)pEnv)->runtimeContext.taintedFlags[flgIdx] = (rev::DWORD)symbolicValue;
}

void RevSymbolicEnvironment::UnsetFlgValue(rev::BYTE flg) {
	SetFlgValue(flg, nullptr);
}