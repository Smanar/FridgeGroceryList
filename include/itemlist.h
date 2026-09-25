#ifndef ITEMLIST_H
#define ITEMLIST_H

void initItemList(void);
const char *GetItemsFromList(short i);
bool addItemList(const std::string& s);
bool removeItemList(const std::string& lookFor);
int GetTotalItem(void);
void ClearList(void);
void UpdateListToSN(void);

#endif