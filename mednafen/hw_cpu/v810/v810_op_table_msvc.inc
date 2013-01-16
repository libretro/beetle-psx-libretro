switch(tmpop >> 9)
{
	case 0:
	case 1:
		goto op_MOV;
		break;
	case 2:
	case 3:
		goto op_ADD;
		break;
	case 4:
	case 5:
		goto op_SUB;
		break;
	case 6:
	case 7:
		goto op_CMP;
		break;
	case 8:
	case 9:
		goto op_SHL;
		break;
	case 10:
	case 11:
		goto op_SHR;
		break;
	case 12:
	case 13:
		goto op_JMP;
		break;
	case 14:
	case 15:
		goto op_SAR;
		break;
	case 16:
	case 17:
		goto op_MUL;
		break;
	case 18:
	case 19:
		goto op_DIV;
		break;
	case 20:
	case 21:
		goto op_MULU;
		break;
	case 22:
	case 23:
		goto op_DIVU;
		break;
	case 24:
	case 25:
		goto op_OR;
		break;
	case 26:
	case 27:
		goto op_AND;
		break;
	case 28:
	case 29:
		goto op_XOR;
		break;
	case 30:
	case 31:
		goto op_NOT;
		break;
	case 32:
	case 33:
		goto op_MOV_I;
		break;
	case 34:
	case 35:
		goto op_ADD_I;
		break;
	case 36:
	case 37:
		goto op_SETF;
		break;
	case 38:
	case 39:
		goto op_CMP_I;
		break;
	case 40:
	case 41:
		goto op_SHL_I;
		break;
	case 42:
	case 43:
		goto op_SHR_I;
		break;
	case 44:
	case 45:
		goto op_EI;
		break;
	case 46:
	case 47:
		goto op_SAR_I;
		break;
	case 48:
	case 49:
		goto op_TRAP;
		break;
	case 50:
	case 51:
		goto op_RETI;
		break;
	case 52:
	case 53:
		goto op_HALT;
		break;
	case 54:
	case 55:
		goto op_INVALID;
		break;
	case 56:
	case 57:
		goto op_LDSR;
		break;
	case 58:
	case 59:
		goto op_STSR;
		break;
	case 60:
	case 61:
		goto op_DI;
		break;
	case 62:
	case 63:
		goto op_BSTR;
		break;
	case 64:
		goto op_BV;
		break;
	case 65:
		goto op_BL;
		break;
	case 66:
		goto op_BE;
		break;
	case 67:
		goto op_BNH;
		break;
	case 68:
		goto op_BN;
		break;
	case 69:
		goto op_BR;
		break;
	case 70:
		goto op_BLT;
		break;
	case 71:
		goto op_BLE;
		break;
	case 72:
		goto op_BNV;
		break;
	case 73:
		goto op_BNL;
		break;
	case 74:
		goto op_BNE;
		break;
	case 75:
		goto op_BH;
		break;
	case 76:
		goto op_BP;
		break;
	case 77:
		goto op_NOP;
		break;
	case 78:
		goto op_BGE;
		break;
	case 79:
		goto op_BGT;
		break;
	case 80:
	case 81:
		goto op_MOVEA;
		break;
	case 82:
	case 83:
		goto op_ADDI;
		break;
	case 84:
	case 85:
		goto op_JR;
		break;
	case 86:
	case 87:
		goto op_JAL;
		break;
	case 88:
	case 89:
		goto op_ORI;
		break;
	case 90:
	case 91:
		goto op_ANDI;
		break;
	case 92:
	case 93:
		goto op_XORI;
		break;
	case 94:
	case 95:
		goto op_MOVHI;
		break;
	case 96:
	case 97:
		goto op_LD_B;
		break;
	case 98:
	case 99:
		goto op_LD_H;
		break;
	case 100:
	case 101:
		goto op_INVALID;
		break;
	case 102:
	case 103:
		goto op_LD_W;
		break;
	case 104:
	case 105:
		goto op_ST_B;
		break;
	case 106:
	case 107:
		goto op_ST_H;
		break;
	case 108:
	case 109:
		goto op_INVALID;
		break;
	case 110:
	case 111:
		goto op_ST_W;
		break;
	case 112:
	case 113:
		goto op_IN_B;
		break;
	case 114:
	case 115:
		goto op_IN_H;
		break;
	case 116:
	case 117:
		goto op_CAXI;
		break;	
	case 118:
	case 119:
		goto op_IN_W;
		break;
	case 120:
	case 121:
		goto op_OUT_B;
		break;
	case 122:
	case 123:
		goto op_OUT_H;
		break;
	case 124:
	case 125:
		goto op_FPP;
		break;
	case 126:
	case 127:
		goto op_OUT_W;
		break;
	case 255:
		goto op_INT_HANDLER;
		break;
	default:
		goto op_INVALID;
		break;
}
