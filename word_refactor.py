import re

with open('words.h','r') as f:
    words_text=f.read()

the_map={}

def replaceall(a,b,c):
    while a != a.replace(b,c):
        print(" repl")
        a=a.replace(b,c)
    return a

for line in words_text.split("\n"):
    line=re.sub(" +"," ",line)
    if line.startswith("#define"):
        [_, replace, find] = line.split(" ")[:3]
        the_map[find]=replace

# print(the_map)

with open('interp.c','r') as f:
    text=f.read()
    for find in the_map:
        print(f"{find} -> {the_map[find]}")
        text=replaceall(text,find,the_map[find])
    # print(text)
with open('interp.c','w') as f:
    f.write(text)
