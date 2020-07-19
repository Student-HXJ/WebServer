cmd=$1

if [ ${cmd} == "git" ]; then
  git add . && git commit -m ""$(date +%Y-%m-%d)"" && git push
else
  echo "No selection"
fi