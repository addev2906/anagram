while true; do
    until pgrep -x anagram >/dev/null; do sleep 0.2; done
    tail -F --pid=$(pgrep -n -x anagram) log.txt
    clear
done
