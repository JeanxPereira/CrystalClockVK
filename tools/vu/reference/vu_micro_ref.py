import struct

def get_special_bit(instruction):

	sbits = (instruction >> 27) & 0xF
	string = ""
	if ((sbits >> 3) & 1):
		string += "[E]"
	if ((sbits >> 2) & 1):
		string += "[M]"
	if ((sbits >> 1) & 1):
		string += "[D]"
	if (sbits & 1):
		string += "[T]"

	return string

def get_2bit_field(bc):

	if (bc == 0):
		return "x"
	if (bc == 1):
		return "y"
	if (bc == 2):
		return "z"
	if (bc == 3):
		return "w"

def get_4bit_field(field):

	if (field == 1):
		return "w"
	elif (field == 2):
		return "z"
	elif (field == 4):
		return "y"
	elif (field == 8):
		return "x"
	elif (field == 3):
		return "zw"
	elif (field == 5):
		return "yw"
	elif (field == 6):
		return "yz"
	elif (field == 9):
		return "xw"
	elif (field == 10):
		return "xz"
	elif (field == 12):
		return "xy"
	elif (field == 7):
		return "yzw"
	elif (field == 11):
		return "xzw"
	elif (field == 13):
		return "xyw"
	elif (field == 14):
		return "xyz"
	elif (field == 15):
		return "xyzw"
	else:
		return "xyzw"

def get_vu1_reg_for_vu0(imm):

	spec_regs = ["status", "mac", "clip", "vi19", "R", "I", "Q", "P", "vi24_reserved", "vi25_reserved",
				"TPC", "vi27_reserved", "vi28_reserved", "vi29_reserved", "vi30_reserved", "vi31_reserved"]
	imm &= 0x3F
	if (imm < 0x20):
		return "vu1_vf{:d}".format(imm)
	elif (imm < 0x30):
		imm -= 0x20
		return "vu1_vi{:d}".format(imm)
	else:
		imm -= 0x30
		return "vu1_" + spec_regs[imm]

def itof(address, instr, dest, source, field, sbits):

	field2 = get_4bit_field(field)

	while len(instr + "." + field2) < 13:
		field2 += " "

	string  = instr + "." + field2 + " vf{:d}, vf{:d}" + sbits
	return string.format(dest, source)

def vu_bc(address, instr, dest, reg1, reg2, field, bc, sbits):

	bc2 = get_2bit_field(bc)
	field2 = get_4bit_field(field)

	if (dest == 34):
		dest_str = " ACC, "
	else:
		dest_str = " vf{:d}, "

	while len(instr + bc2 + "." + field2) < 13:
		field2 += " "

	string  = instr + bc2 + "." + field2 + dest_str + "vf{:d}, vf{:d}" + bc2 + sbits
	if (dest == 34):
		string  = string.format(reg1, reg2)
	else:
		string  = string.format(dest, reg1, reg2)

	return string

def vu_dr1r2f(address, instr, dest, reg1, reg2, field, sbits):

	field2 = get_4bit_field(field)

	if (reg2 == 32):
		reg2_str = ", Q"
	elif (reg2 == 33):
		reg2_str = ", I"
	else:
		reg2_str = ", vf{:d}"

	if (dest == 34):
		dest_str = " ACC, "
	else:
		dest_str = " vf{:d}, "

	while len(instr + "." + field2) < 13:
		field2 += " "

	string  = instr + "." + field2 + dest_str + "vf{:d}" + reg2_str + sbits

	if (reg2 >= 32 and dest == 34):
		string  = string.format(reg1)
	elif (reg2 >= 32 and dest != 34):
		string  = string.format(dest, reg1)
	elif (reg2 < 32 and dest == 34):
		string  = string.format(reg1, reg2)
	else:
		string  = string.format(dest, reg1, reg2)

	return string

def upper(address, instruction):

	op = instruction & 0x3F

	if (op <= 0x03):
		return addbc(address, instruction)
	elif (op >= 0x04 and op <= 0x07):
		return subbc(address, instruction)
	elif (op >= 0x08 and op <= 0x0B):
		return maddbc(address, instruction)
	elif (op >= 0x0C and op <= 0x0F):
		return msubbc(address, instruction)
	elif (op >= 0x10 and op <= 0x13):
		return maxbc(address, instruction)
	elif (op >= 0x14 and op <= 0x17):
		return minibc(address, instruction)
	elif (op >= 0x18 and op <= 0x1B):
		return mulbc(address, instruction)
	elif (op == 0x1C):
		return mulq(address, instruction)
	elif (op == 0x1D):
		return maxi(address, instruction)
	elif (op == 0x1E):
		return muli(address, instruction)
	elif (op == 0x1F):
		return minii(address, instruction)
	elif (op == 0x20):
		return addq(address, instruction)
	elif (op == 0x21):
		return maddq(address, instruction)
	elif (op == 0x22):
		return addi(address, instruction)
	elif (op == 0x23):
		return maddi(address, instruction)
	elif (op == 0x24):
		return subq(address, instruction)
	elif (op == 0x25):
		return msubq(address, instruction)
	elif (op == 0x26):
		return subi(address, instruction)
	elif (op == 0x27):
		return msubi(address, instruction)
	elif (op == 0x28):
		return add(address, instruction)
	elif (op == 0x29):
		return madd(address, instruction)
	elif (op == 0x2A):
		return mul(address, instruction)
	elif (op == 0x2B):
		return _max(address, instruction)
	elif (op == 0x2C):
		return sub(address, instruction)
	elif (op == 0x2D):
		return msub(address, instruction)
	elif (op == 0x2E):
		return opmsub(address, instruction)
	elif (op == 0x2F):
		return mini(address, instruction)
	elif (op >= 0x3C and op <= 0x3F):
		return upper_special(address, instruction)
	return None

def ref_upper(word: int, loi_word: int | None = None) -> str:

	lines = []
	if ((word >> 31) == 1) and (loi_word is not None):
		val = struct.pack('>I', loi_word & 0xFFFFFFFF)
		val = struct.unpack('>f', val)[0]
		val = str(val)
		lines.append("loi           " + val)

	result = upper(0, word)
	if result is not None:
		lines.append(result)

	return "\n".join(lines)

def addbc(address, instruction):

	bc = instruction & 0x3
	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "add", dest, source, bc_reg, field, bc, sbits)


def subbc(address, instruction):

	bc = instruction & 0x3
	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "sub", dest, source, bc_reg, field, bc, sbits)

def maddbc(address, instruction):

	bc = instruction & 0x3
	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "madd", dest, source, bc_reg, field, bc, sbits)

def msubbc(address, instruction):

	bc = instruction & 0x3
	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "msub", dest, source, bc_reg, field, bc, sbits)

def maxbc(address, instruction):

	bc = instruction & 0x3
	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "max", dest, source, bc_reg, field, bc, sbits)

def minibc(address, instruction):

	bc = instruction & 0x3
	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "mini", dest, source, bc_reg, field, bc, sbits)

def mulbc(address, instruction):

	bc = instruction & 0x3
	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "mul", dest, source, bc_reg, field, bc, sbits)

def mulq(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "mulq", dest, source, 32, field, sbits)

def maxi(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "maxi", dest, source, 33, field, sbits)

def muli(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "muli", dest, source, 33, field, sbits)

def minii(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "minii", dest, source, 33, field, sbits)

def addq(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "addq", dest, source, 32, field, sbits)

def maddq(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "maddq", dest, source,32, field, sbits)

def addi(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "addi", dest, source, 33, field, sbits)

def maddi(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "maddi", dest, source, 33, field, sbits)

def subq(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "subq", dest, source, 32, field, sbits)

def msubq(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "msubq", dest, source, 32, field, sbits)

def subi(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "subi", dest, source, 33, field, sbits)

def msubi(address, instruction):

	dest = (instruction >> 6) & 0x1F
	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "msubi", dest, source, 33, field, sbits)

def add(address, instruction):

	dest = (instruction >> 6) & 0x1F
	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "add", dest, reg1, reg2, field, sbits)

def madd(address, instruction):

	dest = (instruction >> 6) & 0x1F
	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "madd", dest, reg1, reg2, field, sbits)

def mul(address, instruction):

	dest = (instruction >> 6) & 0x1F
	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "mul", dest, reg1, reg2, field, sbits)

def _max(address, instruction):

	dest = (instruction >> 6) & 0x1F
	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "max", dest, reg1, reg2, field, sbits)

def sub(address, instruction):

	dest = (instruction >> 6) & 0x1F
	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "sub", dest, reg1, reg2, field, sbits)

def msub(address, instruction):

	dest = (instruction >> 6) & 0x1F
	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "msub", dest, reg1, reg2, field, sbits)

def opmsub(address, instruction):

	dest = (instruction >> 6) & 0x1F
	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	string = "opmsub.xyz    vf{:d}, vf{:d}, vf{:d}"
	string += get_special_bit(instruction)
	return string.format(dest, reg1, reg2)

def mini(address, instruction):

	dest = (instruction >> 6) & 0x1F
	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "mini", dest, reg1, reg2, field, sbits)

##########################################################
def iadd(address, instruction):

	dest = (instruction >> 6) & 0xF
	reg1 = (instruction >> 11) & 0xF
	reg2 = (instruction >> 16) & 0xF
	string = "iadd          vi{:d}, vi{:d}, vi{:d}"
	return string.format(dest, reg1, reg2)

def isub(address, instruction):

	dest = (instruction >> 6) & 0xF
	reg1 = (instruction >> 11) & 0xF
	reg2 = (instruction >> 16) & 0xF
	string = "isub          vi{:d}, vi{:d}, vi{:d}"
	return string.format(dest, reg1, reg2)

def iaddi(address, instruction):

	reg1 = (instruction >> 11) & 0xF
	dest = (instruction >> 16) & 0xF
	imm5 = (instruction >> 6) & 0x1F
	sign = ""
	if imm5 > 0xF:
		imm5 = ~imm5
		imm5 &= 0xF
		imm5 += 1
		sign = "-"
	string = "iaddi         vi{:d}, vi{:d}, " + sign + "0x{:X}"
	return string.format(dest, reg1, imm5)

def iand(address, instruction):

	dest = (instruction >> 6) & 0xF
	reg1 = (instruction >> 11) & 0xF
	reg2 = (instruction >> 16) & 0xF
	string = "iand          vi{:d}, vi{:d}, vi{:d}"
	return string.format(dest, reg1, reg2)

def ior(address, instruction):

	dest = (instruction >> 6) & 0xF
	reg1 = (instruction >> 11) & 0xF
	reg2 = (instruction >> 16) & 0xF
	string = "ior           vi{:d}, vi{:d}, vi{:d}"
	return string.format(dest, reg1, reg2)

def upper_special(address, instruction):

	op = (instruction & 0x3) | ((instruction >> 4) & 0x7C)

	if (op <= 0x03):
		return addabc(address, instruction)
	elif (op >= 0x04 and op <= 0x07):
		return subabc(address, instruction)
	elif (op >= 0x08 and op <= 0x0B):
		return maddabc(address, instruction)
	elif (op >= 0x0C and op <= 0x0F):
		return msubabc(address, instruction)
	elif (op == 0x10):
		return itof0(address, instruction)
	elif (op == 0x11):
		return itof4(address, instruction)
	elif (op == 0x12):
		return itof12(address, instruction)
	elif (op == 0x13):
		return itof15(address, instruction)
	elif (op == 0x14):
		return ftoi0(address, instruction)
	elif (op == 0x15):
		return ftoi4(address, instruction)
	elif (op == 0x16):
		return ftoi12(address, instruction)
	elif (op == 0x17):
		return ftoi15(address, instruction)
	elif (op >= 0x18 and op <= 0x1B):
		return mulabc(address, instruction)
	elif (op == 0x1C):
		return mulaq(address, instruction)
	elif (op == 0x1D):
		return _abs(address, instruction)
	elif (op == 0x1E):
		return mulai(address, instruction)
	elif (op == 0x1F):
		return clip(address, instruction)
	elif (op == 0x20):
		return addaq(address, instruction)
	elif (op == 0x21):
		return maddaq(address, instruction)
	elif (op == 0x22):
		return addai(address, instruction)
	elif (op == 0x23):
		return maddai(address, instruction)
	elif (op == 0x25):
		return msubaq(address, instruction)
	elif (op == 0x26):
		return subai(address, instruction)
	elif (op == 0x27):
		return msubai(address, instruction)
	elif (op == 0x28):
		return adda(address, instruction)
	elif (op == 0x29):
		return madda(address, instruction)
	elif (op == 0x2A):
		return mula(address, instruction)
	elif (op == 0x2C):
		return suba(address, instruction)
	elif (op == 0x2D):
		return msuba(address, instruction)
	elif (op == 0x2E):
		return opmula(address, instruction)
	elif (op == 0x2F):
		return nop(address, instruction)
	return None


def lower(address, instruction):

	if (instruction == 0x8000033C):
		return "nop"
	if (instruction & (1 << 31)):
		return lower1(address, instruction)
	else:
		return lower2(address, instruction)

def ref_lower(word: int) -> str:
	return lower(0, word)

def lower1(address, instruction):

	op = (instruction & 0x3F)

	if (op == 0x30):
		return iadd(address, instruction)
	elif (op == 0x31):
		return isub(address, instruction)
	elif (op == 0x32):
		return iaddi(address, instruction)
	elif (op == 0x34):
		return iand(address, instruction)
	elif (op == 0x35):
		return ior(address, instruction)
	elif (op >= 0x3C and op <= 0x3F):
		return lower1_special(address, instruction)
	return None

def lower1_special(address, instruction):

	op = (instruction & 0x3) | ((instruction >> 4) & 0x7C)

	if (op == 0x30):
		return move(address, instruction)
	elif (op == 0x31):
		return mr32(address, instruction)
	elif (op == 0x34):
		return lqi(address, instruction)
	elif (op == 0x35):
		return sqi(address, instruction)
	elif (op == 0x36):
		return lqd(address, instruction)
	elif (op == 0x37):
		return sqd(address, instruction)
	elif (op == 0x38):
		return div(address, instruction)
	elif (op == 0x39):
		return sqrt(address, instruction)
	elif (op == 0x3A):
		return rsqrt(address, instruction)
	elif (op == 0x3B):
		return waitq(address, instruction)
	elif (op == 0x3C):
		return mtir(address, instruction)
	elif (op == 0x3D):
		return mfir(address, instruction)
	elif (op == 0x3E):
		return ilwr(address, instruction)
	elif (op == 0x3F):
		return iswr(address, instruction)
	elif (op == 0x40):
		return rnext(address, instruction)
	elif (op == 0x41):
		return rget(address, instruction)
	elif (op == 0x42):
		return rinit(address, instruction)
	elif (op == 0x43):
		return rxor(address, instruction)
	elif (op == 0x64):
		return mfp(address, instruction)
	elif (op == 0x68):
		return xtop(address, instruction)
	elif (op == 0x69):
		return xitop(address, instruction)
	elif (op == 0x6C):
		return xgkick(address, instruction)
	elif (op == 0x70):
		return esadd(address, instruction)
	elif (op == 0x71):
		return ersadd(address, instruction)
	elif (op == 0x72):
		return eleng(address, instruction)
	elif (op == 0x73):
		return erleng(address, instruction)
	elif (op == 0x74):
		return eatanxy(address, instruction)
	elif (op == 0x75):
		return eatanxz(address, instruction)
	elif (op == 0x76):
		return esum(address, instruction)
	elif (op == 0x78):
		return esqrt(address, instruction)
	elif (op == 0x79):
		return ersqrt(address, instruction)
	elif (op == 0x7A):
		return ercpr(address, instruction)
	elif (op == 0x7B):
		return "waitp"
	elif (op == 0x7C):
		return esin(address, instruction)
	elif (op == 0x7D):
		return eatan(address, instruction)
	elif (op == 0x7E):
		return eexp(address, instruction)
	return None

def mfp(address, instruction):

	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(field)

	while len("mfp." + field2) < 13:
		field2 += " "

	string = "mfp." + field2 + " vf{:d}, P"
	return string.format(dest)


def xtop(address, instruction):

	it = (instruction >> 16) & 0x1F
	string = "xtop          vi{:d}"
	return string.format(it)


def xitop(address, instruction):

	it = (instruction >> 16) & 0x1F
	string = "xitop         vi{:d}"
	return string.format(it)


def xgkick(address, instruction):

	_is = (instruction >> 11) & 0x1F
	string = "xgkick        vi{:d}"
	return string.format(_is)


def esadd(address, instruction):

	source = (instruction >> 11) & 0x1F
	string = "esadd         P, vf{:d}"
	return string.format(source)


def ersadd(address, instruction):

	source = (instruction >> 11) & 0x1F
	string = "ersadd        P, vf{:d}"
	return string.format(source)


def eleng(address, instruction):

	source = (instruction >> 11) & 0x1F
	string = "eleng         P, vf{:d}"
	return string.format(source)


def esum(address, instruction):

	source = (instruction >> 11) & 0x1F
	string = "esum          P, vf{:d}"
	return string.format(source)


def ercpr(address, instruction):

	source = (instruction >> 11) & 0x1F
	fsf = (instruction >> 21) & 0x3
	fsf2 = get_2bit_field(fsf)
	string = "ercpr         P, vf{:d}." + fsf2
	return string.format(source)


def erleng(address, instruction):

	source = (instruction >> 11) & 0x1F
	string = "erleng        P, vf{:d}"
	return string.format(source)


def esqrt(address, instruction):

	source = (instruction >> 11) & 0x1F
	fsf = (instruction >> 21) & 0x3
	fsf2 = get_2bit_field(fsf)
	string = "esqrt         P, vf{:d}." + fsf2
	return string.format(source)


def ersqrt(address, instruction):

	source = (instruction >> 11) & 0x1F
	fsf = (instruction >> 21) & 0x3
	fsf2 = get_2bit_field(fsf)
	string = "ersqrt        P, vf{:d}." + fsf2
	return string.format(source)


def esin(address, instruction):

	source = (instruction >> 11) & 0x1F
	fsf = (instruction >> 21) & 0x3
	fsf2 = get_2bit_field(fsf)
	string = "esin          P, vf{:d}." + fsf2
	return string.format(source)


def eatan(address, instruction):

	source = (instruction >> 11) & 0x1F
	fsf = (instruction >> 21) & 0x3
	fsf2 = get_2bit_field(fsf)
	string = "eatan         P, vf{:d}." + fsf2
	return string.format(source)


def eatanxy(address, instruction):

	source = (instruction >> 11) & 0x1F
	string = "eatanxy       P, vf{:d}"
	return string.format(source)


def eatanxz(address, instruction):

	source = (instruction >> 11) & 0x1F
	string = "eatanxz       P, vf{:d}"
	return string.format(source)


def eexp(address, instruction):

	source = (instruction >> 11) & 0x1F
	fsf = (instruction >> 21) & 0x3
	fsf2 = get_2bit_field(fsf)
	string = "eexp          P, vf{:d}." + fsf2
	return string.format(source)


def lower2(address, instruction):

	op = ((instruction >> 25) & 0x7F)

	if (op == 0x00):
		return lq(address, instruction)
	elif (op  == 0x01):
		return sq(address, instruction)
	elif (op  == 0x04):
		return loadstore_imm(address, "ilw", instruction)
	elif (op  == 0x05):
		return loadstore_imm(address, "isw", instruction)
	elif (op  == 0x08):
		return arithu(address, "iaddiu", instruction)
	elif (op  == 0x09):
		return arithu(address, "isubiu", instruction)
	elif (op  == 0x10):
		return fceq(address, instruction)
	elif (op  == 0x11):
		return fcset(address, instruction)
	elif (op  == 0x12):
		return fcand(address, instruction)
	elif (op  == 0x13):
		return fcor(address, instruction)
	elif (op  == 0x14):
		return fseq(address, instruction)
	elif (op  == 0x15):
		return fsset(address, instruction)
	elif (op  == 0x16):
		return fsand(address, instruction)
	elif (op  == 0x17):
		return fsor(address, instruction)
	elif (op  == 0x18):
		return fmeq(address, instruction)
	elif (op  == 0x1A):
		return fmand(address, instruction)
	elif (op  == 0x1B):
		return fmor(address, instruction)
	elif (op  == 0x1C):
		return fcget(address, instruction)
	elif (op  == 0x20):
		return b(address, instruction)
	elif (op  == 0x21):
		return bal(address, instruction)
	elif (op  == 0x24):
		return jr(address, instruction)
	elif (op  == 0x25):
		return jalr(address, instruction)
	elif (op  == 0x28):
		return branch(address, "ibeq", instruction)
	elif (op  == 0x29):
		return branch(address, "ibne", instruction)
	elif (op  == 0x2C):
		return branch_zero(address, "ibltz", instruction)
	elif (op  == 0x2D):
		return branch_zero(address, "ibgtz", instruction)
	elif (op  == 0x2E):
		return branch_zero(address, "iblez", instruction)
	elif (op  == 0x2F):
		return branch_zero(address, "ibgez", instruction)
	return None



def loadstore_imm(address, string, instruction):

	sign = ""
	_is = (instruction >> 11) & 0x1F
	it = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(field)
	imm = instruction & 0x7FF
	if (imm > 0x3FF and _is != 0):
		imm = ~imm
		imm &= 0x3FF
		imm += 1
		sign = "-"
	elif (imm > 0x3FF and _is == 0):
		vu1_reg = get_vu1_reg_for_vu0(imm)
		while len(string + "." + field2) < 13:
			field2 += " "
		string = string + "." + field2 + " vi{:d}, " + vu1_reg
		return string.format(it)
	imm *= 16
	while len(string + "." + field2) < 13:
		field2 += " "

	string = string + "." + field2 + " vi{:d}, " + sign + "0x{:X}(vi{:d})"
	return string.format(it, imm, _is)


def arithu(address, string, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	imm = instruction & 0x7FF
	imm |= ((instruction >> 21) & 0xF) << 11
	while len(string) < 13:
		string += " "

	string = string + " vi{:d}, vi{:d}, 0x{:X}"
	return string.format(dest, source, imm)


def branch(address, string, instruction):

	imm = instruction & 0x7FF
	_is = (instruction >> 11) & 0x1F
	it = (instruction >> 16) & 0x1F
	if (imm > 0x3FF):
		imm &= 0x3FF
		imm = ~imm
		imm &= 0x3FF
		imm *= 8
		addr = (address - imm)
		addr -= skip_vif_data(addr, address)

		while len(string) < 13:
			string += " "

		string = string + " vi{:d}, vi{:d}, 0x{:X}"
		return string.format(it, _is, addr)

	imm *= 8
	addr = address + imm + 8
	addr += skip_vif_data(address, addr)
	while len(string) < 13:
		string += " "

	string = string + " vi{:d}, vi{:d}, 0x{:X}"
	return string.format(it, _is, addr)


def branch_zero(address, string, instruction):

	imm = instruction & 0x7FF
	reg = (instruction >> 11) & 0x1F
	if (imm > 0x3FF):
		imm &= 0x3FF
		imm = ~imm
		imm &= 0x3FF
		imm *= 8
		addr = (address - imm) #+ 8
		addr -= skip_vif_data(addr, address)

		while len(string) < 13:
			string += " "

		string = string + " vi{:d}, 0x{:X}"
		return string.format(reg, addr)

	imm *= 8
	addr = address + imm + 8
	addr += skip_vif_data(address, addr)
	while len(string) < 13:
		string += " "

	string = string + " vi{:d}, 0x{:X}"
	return string.format(reg, addr)

def b(address, instruction):

	imm = instruction & 0x7FF
	if (imm > 0x3FF):
		imm &= 0x3FF
		imm = ~imm
		imm &= 0x3FF
		imm *= 8
		addr = (address - imm)
		addr -= skip_vif_data(addr, address)
		string = "b             0x{:X}"
		return string.format(addr)

	imm *= 8
	addr = address + imm + 8
	addr += skip_vif_data(address, addr)
	string = "b             0x{:X}"
	return string.format(addr)


def bal(address, instruction):

	imm = instruction & 0x7FF
	link_reg = (instruction >> 16) & 0x1F
	if (imm > 0x3FF):
		imm &= 0x3FF
		imm = ~imm
		imm &= 0x3FF
		imm *= 8
		addr = (address - imm)
		addr -= skip_vif_data(addr, address)
		string = "bal           vi{:d} 0x{:X}"
		return string.format(link_reg, addr)

	imm *= 8
	addr = address + imm + 8
	addr += skip_vif_data(address, addr)
	string = "bal           vi{:d} 0x{:X}"
	return string.format(link_reg, addr)


def jr(address, instruction):

	addr_reg = (instruction >> 11) & 0x1F
	string = "jr            vi{:d}"
	return string.format(addr_reg)


def jalr(address, instruction):

	addr_reg = (instruction >> 11) & 0x1F
	link_reg = (instruction >> 16) & 0x1F
	string = "jalr          vi{:d}, vi{:d}"
	return string.format(link_reg, addr_reg)


def lq(address, instruction):

	sign = ""
	_is = (instruction >> 11) & 0x1F
	ft = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(field)
	imm = instruction & 0x7FF
	if (imm > 0x3FF and _is != 0):
		imm = ~imm
		imm &= 0x3FF
		imm += 1
		sign = "-"
	elif (imm > 0x3FF and _is == 0):
		vu1_reg = get_vu1_reg_for_vu0(imm)
		while len("lq." + field2) < 13:
			field2 += " "
		string = "lq." + field2 + " vf{:d}, " + vu1_reg
		return string.format(ft)
	imm *= 16

	while len("lq." + field2) < 13:
		field2 += " "

	string = "lq." + field2 + " vf{:d}, " + sign + "0x{:X}(vi{:d})"
	return string.format(ft, imm, _is)


def sq(address, instruction):

	sign = ""
	it = (instruction >> 16) & 0x1F
	fs = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(field)
	imm = instruction & 0x7FF
	if (imm > 0x3FF and it != 0):
		imm = ~imm
		imm &= 0x3FF
		imm += 1
		sign = "-"
	elif (imm > 0x3FF and it == 0):
		vu1_reg = get_vu1_reg_for_vu0(imm)
		while len("sq." + field2) < 13:
			field2 += " "
		string = "sq." + field2 + " vf{:d}, " + vu1_reg
		return string.format(fs)
	imm *= 16

	while len("sq." + field2) < 13:
		field2 += " "

	string = "sq." + field2 + " vf{:d}, " + sign + "0x{:X}(vi{:d})"
	return string.format(fs, imm, it)


def fceq(address, instruction):

	imm = instruction & 0xFFFFFF
	string = "fceq          vi1, 0x{:X}"
	return string.format(imm)


def fcset(address, instruction):

	imm = instruction & 0xFFFFFF
	string = "fcset         0x{:X}"
	return string.format(imm)


def fcand(address, instruction):

	imm = instruction & 0xFFFFFF
	string = "fcand         vi1, 0x{:X}"
	return string.format(imm)


def fcor(address, instruction):

	imm = instruction & 0xFFFFFF
	string = "fcor          vi1, 0x{:X}"
	return string.format(imm)


def fseq(address, instruction):

	imm = ((instruction >> 10) & 0x800) | (instruction & 0x7FF)
	dest = (instruction >> 16) & 0x1F
	string = "fseq          vi{:d}, 0x{:X}"
	return string.format(dest, imm)


def fsset(address, instruction):

	imm = ((instruction >> 10) & 0x800) | (instruction & 0x7FF)
	string = "fsset         0x{:X}"
	return string.format(imm)


def fsand(address, instruction):

	imm = ((instruction >> 10) & 0x800) | (instruction & 0x7FF)
	dest = (instruction >> 16) & 0x1F
	string = "fsand         vi{:d}, 0x{:X}"
	return string.format(dest, imm)


def fsor(address, instruction):

	imm = ((instruction >> 10) & 0x800) | (instruction & 0x7FF)
	dest = (instruction >> 16) & 0x1F
	string = "fsor          vi{:d}, 0x{:X}"
	return string.format(dest, imm)


def fmeq(address, instruction):

	_is = (instruction >> 11) & 0x1F
	it = (instruction >> 16) & 0x1F
	string = "fmeq          vi{:d}, vi{:d}"
	return string.format(it, _is)


def fmand(address, instruction):

	_is = (instruction >> 11) & 0x1F
	it = (instruction >> 16) & 0x1F
	string = "fmand         vi{:d}, vi{:d}"
	return string.format(it, _is)


def fmor(address, instruction):

	_is = (instruction >> 11) & 0x1F
	it = (instruction >> 16) & 0x1F
	string = "fmor          vi{:d}, vi{:d}"
	return string.format(it, _is)


def fcget(address, instruction):

	it = (instruction >> 16) & 0x1F
	string = "fcget         vi{:d}"
	return string.format(it)


def skip_vif_data(start, end):
	return 0

#####################################################
def addabc(address, instruction):

	bc = instruction & 0x3
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "adda", 34, source, bc_reg, field, bc, sbits)

def subabc(address, instruction):

	bc = instruction & 0x3
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "suba", 34, source, bc_reg, field, bc, sbits)

def maddabc(address, instruction):

	bc = instruction & 0x3
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "madda", 34, source, bc_reg, field, bc, sbits)

def msubabc(address, instruction):

	bc = instruction & 0x3
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "msuba", 34, source, bc_reg, field, bc, sbits)

def itof0(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return itof(address, "itof0", dest, source, field, sbits)

def itof4(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return itof(address, "itof4", dest, source, field, sbits)

def itof12(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return itof(address, "itof12", dest, source, field, sbits)

def itof15(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return itof(address, "itof15", dest, source, field, sbits)

def ftoi0(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return itof(address, "ftoi0", dest, source, field, sbits)

def ftoi4(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return itof(address, "ftoi4", dest, source, field, sbits)

def ftoi12(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return itof(address, "ftoi12", dest, source, field, sbits)

def ftoi15(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return itof(address, "ftoi15", dest, source, field, sbits)

def mulabc(address, instruction):

	bc = instruction & 0x3
	source = (instruction >> 11) & 0x1F
	bc_reg = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_bc(address, "mula", 34, source, bc_reg, field, bc, sbits)

def mulaq(address, instruction):

	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "mulaq", 34, source, 32, field, sbits)

def _abs(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return itof(address, "abs", dest, source, field, sbits)

def mulai(address, instruction):

	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "mulai", 34, source, 33, field, sbits)

def clip(address, instruction):

	instr = "clipw."
	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	field = get_4bit_field(field)
	while len(instr + field) < 13:
		field += " "
	string = instr + field + " vf{:d}, vf{:d}w"
	return string.format(reg1, reg2)

def addaq(address, instruction):

	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "addaq", 34, source, 32, field, sbits)

def maddaq(address, instruction):

	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "maddaq", 34, source, 32, field, sbits)

def maddai(address, instruction):

	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "maddai", 34, source, 33, field, sbits)

def msubaq(address, instruction):

	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "msubaq", 34, source, 32, field, sbits)

def subai(address, instruction):

	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "subai", 34, source, 33, field, sbits)

def msubai(address, instruction):

	source = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "msubai", 34, source, 33, field, sbits)

def adda(address, instruction):

	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "adda", 34, reg1, reg2, field, sbits)

def addai(address, instruction):

	reg1 = (instruction >> 11) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "addai", 34, reg1, 33, field, sbits)

def madda(address, instruction):

	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "madda", 34, reg1, reg2, field, sbits)

def mula(address, instruction):

	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "mula", 34, reg1, reg2, field, sbits)

def suba(address, instruction):

	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "suba", 34, reg1, reg2, field, sbits)

def msuba(address, instruction):

	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	sbits = get_special_bit(instruction)
	return vu_dr1r2f(address, "msuba", 34, reg1, reg2, field, sbits)

def opmula(address, instruction):

	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	string = "opmula.xyz    ACC, vf{:d}, vf{:d}"
	return string.format(reg1, reg2)

def nop(address, instruction):

	sbits = get_special_bit(instruction)
	return "nop           " + sbits

def move(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	return itof(address, "move", dest, source, field, "")

def mr32(address, instruction):

	source = (instruction >> 11) & 0x1F
	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	return itof(address, "mr32", dest, source, field, "")

def lqi(address, instruction):

	_is = (instruction >> 11) & 0xF
	ft = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(field)

	while len("lqi." + field2) < 13:
		field2 += " "

	string = "lqi." + field2 + " vf{:d}, (vi{:d}++)"
	return string.format(ft, _is)

def sqi(address, instruction):

	fs = (instruction >> 11) & 0x1F
	it = (instruction >> 16) & 0xF
	dest_field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(dest_field)

	while len("sqi." + field2) < 13:
		field2 += " "

	string = "sqi." + field2 + " vf{:d}, (vi{:d}++)"
	return string.format(fs, it)

def lqd(address, instruction):

	_is = (instruction >> 11) & 0xF
	ft = (instruction >> 16) & 0x1F
	dest_field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(dest_field)

	while len("lqd." + field2) < 13:
		field2 += " "

	string = "lqd." + field2 + " vf{:d}, (--vi{:d})"
	return string.format(ft, _is)

def sqd(address, instruction):

	fs = (instruction >> 11) & 0x1F
	it = (instruction >> 16) & 0xF
	dest_field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(dest_field)

	while len("sqd." + field2) < 13:
		field2 += " "

	string = "sqd." + field2 + " vf{:d}, (--vi{:d})"
	return string.format(fs, it)

def div(address, instruction):

	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	fsf = (instruction >> 21) & 0x3
	ftf = (instruction >> 23) & 0x3
	fsf2 = get_2bit_field(fsf)
	ftf2 = get_2bit_field(ftf)
	string = "div           Q, vf{:d}" + fsf2 + " vf{:d}" + ftf2

	return string.format(reg1, reg2)

def sqrt(address, instruction):

	source = (instruction >> 16) & 0x1F
	ftf = (instruction >> 23) & 0x3
	ftf2 = get_2bit_field(ftf)
	string = "sqrt          Q, vf{:d}" + ftf2

	return string.format(source)

def rsqrt(address, instruction):

	reg1 = (instruction >> 11) & 0x1F
	reg2 = (instruction >> 16) & 0x1F
	fsf = (instruction >> 21) & 0x3
	ftf = (instruction >> 23) & 0x3
	fsf2 = get_2bit_field(fsf)
	ftf2 = get_2bit_field(ftf)
	string = "rsqrt         Q, vf{:d}" + fsf2 + " vf{:d}" + ftf2

	return string.format(reg1, reg2)

def waitq(address, instruction):

	return "waitq"

def mtir(address, instruction):

	fs = (instruction >> 11) & 0x1F
	it = (instruction >> 16) & 0xF
	fsf = (instruction >> 21) & 0x3
	fsf2 = get_2bit_field(fsf)
	string = "mtir          vi{:d}, vf{:d}" + fsf2
	return string.format(it, fs)

def mfir(address, instruction):

	_is = (instruction >> 11) & 0x1F
	ft = (instruction >> 16) & 0x1F
	dest_field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(dest_field)

	while len("mfir." + field2) < 13:
		field2 += " "

	string = "mfir." + field2 + " vf{:d}, vi{:d}"
	return string.format(ft, _is)

def ilwr(address, instruction):

	_is = (instruction >> 11) & 0x1F
	it = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(field)

	while len("ilwr." + field2) < 13:
		field2 += " "

	string = "ilwr." + field2 + " vi{:d}, (vi{:d})" + field2
	return string.format(it, _is)

def iswr(address, instruction):

	_is = (instruction >> 11) & 0x1F
	it = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(field)

	while len("iswr." + field2) < 13:
		field2 += " "

	string = "iswr." + field2 + " vi{:d}, (vi{:d})" + field2
	return string.format(it, _is)

def rnext(address, instruction):

	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(field)

	while len("rnext." + field2) < 13:
		field2 += " "

	string = "rnext." + field2 + " vf{:d}, R"
	return string.format(dest)

def rget(address, instruction):

	dest = (instruction >> 16) & 0x1F
	field = (instruction >> 21) & 0xF
	field2 = get_4bit_field(field)

	while len("rget." + field2) < 13:
		field2 += " "

	string = "rget." + field2 + " vf{:d}, R"
	return string.format(dest)

def rinit(address, instruction):

	source = (instruction >> 11) & 0x1F
	fsf = (instruction >> 21) & 0x3
	fsf2 = get_2bit_field(fsf)
	string = "rinit         R, vf{:d}." + fsf2
	return string.format(source)

def rxor(address, instruction):

	source = (instruction >> 11) & 0x1F
	fsf = (instruction >> 21) & 0x3
	fsf2 = get_2bit_field(fsf)
	string = "rxor          R, vf{:d}." + fsf2
	return string.format(source)
