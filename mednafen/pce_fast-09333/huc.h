namespace PCE_Fast
{

int HuCLoad(const uint8 *data, uint32 len, uint32 crc32) MDFN_COLD;
int HuCLoadCD(const char *bios_path) MDFN_COLD;
void HuCClose(void) MDFN_COLD;
int HuC_StateAction(StateMem *sm, int load, int data_only);

void HuC_Power(void) MDFN_COLD;

DECLFR(PCE_ACRead);
DECLFW(PCE_ACWrite);

extern bool PCE_IsCD;

};
