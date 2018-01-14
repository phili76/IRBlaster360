Import("env")

build_tag = ""  # you can automate it
env.Replace(PROGNAME="../../IR_Blaster_360/IR_Blaster_360%s" % build_tag)
