bool examine() {
    bool value = firstCondition || firstLongOperand == secondLongOperand || finalCondition;
    if (firstCondition || firstLongOperand == secondLongOperand || finalCondition) {
        report();
    }
    if (firstCondition || (firstLongOperand == secondLongOperand && nextCondition)) {
        report();
    }
    return firstCondition || firstLongOperand == secondLongOperand || finalCondition;
}
