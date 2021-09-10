-- Wireshark disector for ACM protocol

print("Loading ACM...")

local msgs = {
    [0xae] = "Health",
    [0x51] = "Reg",
    [0xe7] = "Trip",
    [0x33] = "Int",
    [0x28] = "Ext",
}

local acm = Proto("ACM", "ACM Protocol")

local fld_id = ProtoField.uint8("acm.id", "ID", base.HEX, msgs)
local fld_flg = ProtoField.uint8("acm.flags", "flags", base.HEX)
local flg_last = ProtoField.uint8("acm.flags.last", "last", base.HEX, {[0]="Not last",[1]="Last"}, 0x01)
local fld_seq = ProtoField.uint16("acm.seq", "Seq")
local fld_tb = ProtoField.uint32("acm.tb", "Timebase")

local fld_reg = ProtoField.uint32("acm.reg", "Register", base.HEX)

local fld_sample = ProtoField.new("Sample", "samp", ftypes.NONE)
local fld_I = ProtoField.uint16("acm.i", "I", base.HEX)
local fld_Q = ProtoField.uint16("acm.q", "Q", base.HEX)
local fld_A = ProtoField.uint32("acm.cur", "Current", base.HEX)
local fld_P = ProtoField.uint32("acm.pha", "Phase", base.HEX)
local fld_S = ProtoField.uint32("acm.int", "Integral", base.HEX)

acm.fields = {fld_id, fld_flg, flg_last, fld_seq, fld_tb,
              fld_reg,
              fld_sample, fld_I, fld_Q, fld_A, fld_P, fld_S}

function acm.dissector (buf, pkt, root)
    pkt.cols.protocol = acm.name
    pkt.cols.info:clear()
    pkt.cols.info:append(pkt.src_port.."->"..pkt.dst_port)

    if buf:len()<8
    then
        pkt.cols.info:append("Invalid (Truncated?)")
        return
    end

    local tree = root:add(acm, buf)

    tree:add(fld_id, buf(0,1))
    local tflags = tree:add(fld_flg, buf(1,1))
    tflags:add(flg_last, buf(1,1))
    tree:add(fld_seq, buf(2,2))
    tree:add(fld_tb, buf(4,4))

    local id = buf(0,1):uint()
    local flags = buf(1,1):uint()
    local seq = buf(2,2):uint()
    local tb = buf(4,4):uint()
    local body = buf(8):tvb()

    pkt.cols.info:append(string.format(" TB=0x%08x", tb))

    local msg_name = msgs[id]
    if msg_name
    then
        pkt.cols.info:append(" "..msg_name)
    else
        pkt.cols.info:append(" Msg: "..id)
    end

    pkt.cols.info:append(" Seq:"..seq)
    if bit.band(flags, 0x01)==1
    then
        pkt.cols.info:append(" ")
    else
        pkt.cols.info:append("+")
    end

    if id==0x51
    then
        while body:len()>=4
        do
            tree:add(fld_reg, body(0, 4))
            body = body(4):tvb()
        end
    elseif id==0x33 or id==0x28 or id==0xe7
    then
        while body:len()>=16
        do
            local sub = tree:add(fld_sample, body(0, 16))
            sub:add(fld_I, body(0, 2))
            sub:add(fld_Q, body(2, 2))
            sub:add(fld_A, body(4, 4))
            sub:add(fld_P, body(8, 4))
            sub:add(fld_S, body(12,4))
            body = body(16):tvb()
        end
    end

    if body:len()>0
    then
        pkt.cols.info:append(" Trailing junk")
    end
end

local utbl = DissectorTable.get("udp.port")
utbl:add(50004, acm)
utbl:add(50005, acm)
utbl:add(50006, acm)
utbl:add(50007, acm)
